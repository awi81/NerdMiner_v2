#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <nvs_flash.h>
#include <nvs.h>
//#include "ShaTests/nerdSHA256.h"
#include "ShaTests/nerdSHA256plus.h"
#include "stratum.h"
#include "mining.h"
#include "utils.h"
#include "monitor.h"
#include "timeconst.h"
#include "drivers/displays/display.h"
#include "drivers/storage/storage.h"
#include <mutex>
#include <list>
#include <map>
#include "mbedtls/sha256.h"
#include "i2c_master.h"

//10 Jobs per second
#define NONCE_PER_JOB_SW 4096
// 64K statt 16K: der Stratum-Task füllt nur alle >= 50 ms nach (und wartet, solange das Display zeichnet);
// mit 4 x 16K Vorrat stand der HW-Miner bei ~790 KH/s 1,7 % der Zeit ohne Job, mit 64K 0,2 % (hw_idle).
#ifndef NONCE_PER_JOB_HW
#define NONCE_PER_JOB_HW 64*1024
#endif

//#define I2C_SLAVE

//#define SHA256_VALIDATE
//#define RANDOM_NONCE
#define RANDOM_NONCE_MASK 0xFFFFC000

#ifdef HARDWARE_SHA265
#include <sha/sha_dma.h>
#include <hal/sha_hal.h>
#include <hal/sha_ll.h>

#if defined(CONFIG_IDF_TARGET_ESP32)
#include <sha/sha_parallel_engine.h>
#endif

#endif

nvs_handle_t stat_handle;

uint32_t templates = 0;
uint32_t hashes = 0;
// Hashes getrennt nach Hardware- und Software-Miner (laufende Summen, dürfen überlaufen; /info)
uint32_t hashesHw = 0;
uint32_t hashesSw = 0;
// Hardware-Treffer, die in Software nachgerechnet wurden, und davon abweichende (/info)
uint32_t hwChecked = 0;
uint32_t hwErrors = 0;
// Wie oft der HW-Miner ohne Job 2 ms warten musste (/info)
uint32_t hwIdle = 0;
// Selbsttest der HW-Schleife beim Start (1 = ok, 0 = falsch, -1 = nicht gelaufen) und Taktdiagnose (/info)
int hwKat = -1;
char hwBench[64] = "";
uint32_t Mhashes = 0;
uint32_t totalKHashes = 0;
uint32_t elapsedKHs = 0;
uint64_t upTime = 0;

volatile uint32_t shares; // increase if blockhash has 32 bits of zeroes
volatile uint32_t valids; // increased if blockhash <= target

// Track best diff
double best_diff = 0.0;

// Variables to hold data from custom textboxes
//Track mining stats in non volatile memory
extern TSettings Settings;

IPAddress serverIP(1, 1, 1, 1); //Temporally save poolIPaddres

//Global work data 
static WiFiClient client;
static miner_data mMiner; //Global miner data (Create a miner class TODO)
mining_subscribe mWorker;
mining_job mJob;
monitor_data mMonitor;
static bool volatile isMinerSuscribed = false;
unsigned long mLastTXtoPool = millis();

int saveIntervals[7] = {5 * 60, 15 * 60, 30 * 60, 1 * 3600, 3 * 3600, 6 * 3600, 12 * 3600};
int saveIntervalsSize = sizeof(saveIntervals)/sizeof(saveIntervals[0]);
int currentIntervalIndex = 0;

bool checkPoolConnection(void) {
  
  if (client.connected()) {
    return true;
  }
  
  isMinerSuscribed = false;

  Serial.println("Client not connected, trying to connect..."); 
  
  //Resolve pool DNS. WiFi.hostByName() returns 0 and writes 0.0.0.0 into serverIP on
  //failure, so check the return value: otherwise a failed first resolve caches 0.0.0.0
  //and the client can never connect until the device is rebooted.
  if(serverIP == IPAddress(1,1,1,1)) {
    if (WiFi.hostByName(Settings.PoolAddress.c_str(), serverIP) != 1 || serverIP == IPAddress(0,0,0,0)) {
      Serial.println("DNS resolve failed, will retry next attempt");
      serverIP = IPAddress(1,1,1,1); //keep unresolved so we retry
      return false;
    }
    Serial.printf("Resolved DNS got: %s\n", serverIP.toString());
  }

  //Try connecting pool IP
  if (!client.connect(serverIP, Settings.PoolPort)) {
    Serial.println("Imposible to connect to : " + Settings.PoolAddress);
    serverIP = IPAddress(1,1,1,1); //force a fresh DNS resolve on next attempt
    return false;
  }

  return true;
}

//Implements a socketKeepAlive function and 
//checks if pool is not sending any data to reconnect again.
//Even connection could be alive, pool could stop sending new job NOTIFY
unsigned long mStart0Hashrate = 0;
bool checkPoolInactivity(unsigned int keepAliveTime, unsigned long inactivityTime, double suggestDifficulty){ 

    unsigned long currentKHashes = (Mhashes*1000) + hashes/1000;
    unsigned long elapsedKHs = currentKHashes - totalKHashes;

    uint32_t time_now = millis();

    // If no shares sent to pool
    // send something to pool to hold socket oppened
    if (time_now < mLastTXtoPool) //32bit wrap
      mLastTXtoPool = time_now;
    if ( time_now > mLastTXtoPool + keepAliveTime)
    {
      mLastTXtoPool = time_now;
      Serial.println("  Sending  : KeepAlive suggest_difficulty");
      //Re-suggest what the pool actually settled on, not DEFAULT_DIFFICULTY. Asking
      //for 0.00015 twice a minute forever, on a pool whose minimum is higher, looks
      //like a misbehaving client from the pool side (BitMaker-hub/NerdMiner_v2#805).
      tx_suggest_difficulty(client, suggestDifficulty);
      /*if(tx_suggest_difficulty(client, DEFAULT_DIFFICULTY)){
        Serial.println("  Sending keepAlive to pool -> Detected client disconnected");
        return true;
      }*/
    }

    if(elapsedKHs == 0){
      //Check if hashrate is 0 during inactivityTIme
      if(mStart0Hashrate == 0) mStart0Hashrate  = time_now; 
      if((time_now-mStart0Hashrate) > inactivityTime) { mStart0Hashrate=0; return true;}
      return false;
    }

  mStart0Hashrate = 0;
  return false;
}

struct JobRequest
{
  uint32_t id;
  uint32_t nonce_start;
  uint32_t nonce_count;
  double difficulty;
  uint8_t sha_buffer[128];
  uint32_t midstate[8];
  uint32_t bake[16];
};

struct JobResult
{
  uint32_t id;
  uint32_t nonce;
  uint32_t nonce_count;
  double difficulty;
  uint8_t hash[32];
  bool hw;  // vom Hardware-SHA-Miner
};

#define JOB_QUEUE_SIZE    4
#define RESULT_QUEUE_SIZE 16

// Queue fester Größe: keine Heap-Allokation pro Job/Ergebnis (vorher std::list<std::shared_ptr<...>>).
// Nicht threadsicher, Zugriff nur unter s_job_mutex.
template <typename T, size_t N>
class RingQueue
{
public:
  size_t size() const { return m_count; }
  bool full() const { return m_count == N; }
  void clear() { m_head = 0; m_count = 0; }
  // Nächster freier Platz oder nullptr, wenn voll
  T* pushSlot()
  {
    if (m_count == N)
      return nullptr;
    T* slot = &m_items[(m_head + m_count) % N];
    ++m_count;
    return slot;
  }
  bool pop(T& out)
  {
    if (m_count == 0)
      return false;
    out = m_items[m_head];
    m_head = (m_head + 1) % N;
    --m_count;
    return true;
  }
private:
  T m_items[N];
  size_t m_head = 0;
  size_t m_count = 0;
};

static std::mutex s_job_mutex;
static RingQueue<JobRequest, JOB_QUEUE_SIZE> s_job_request_list_sw;
#ifdef HARDWARE_SHA265
static RingQueue<JobRequest, JOB_QUEUE_SIZE> s_job_request_list_hw;
#endif
static RingQueue<JobResult, RESULT_QUEUE_SIZE> s_job_result_list;
static volatile uint8_t s_working_current_job_id = 0xFF;

static void JobPush(RingQueue<JobRequest, JOB_QUEUE_SIZE> &job_list,  uint32_t id, uint32_t nonce_start, uint32_t nonce_count, double difficulty,
                    const uint8_t* sha_buffer, const uint32_t* midstate, const uint32_t* bake)
{
  JobRequest* job = job_list.pushSlot();
  if (!job)
    return;
  job->id = id;
  job->nonce_start = nonce_start;
  job->nonce_count = nonce_count;
  job->difficulty = difficulty;
  memcpy(job->sha_buffer, sha_buffer, sizeof(job->sha_buffer));
  memcpy(job->midstate, midstate, sizeof(job->midstate));
  memcpy(job->bake, bake, sizeof(job->bake));
}

// Ergebnis eines Miner-Tasks abliefern (verworfen, wenn die Queue voll ist)
static void ResultPush(const JobResult &result)
{
  JobResult* slot = s_job_result_list.pushSlot();
  if (slot)
    *slot = result;
}

struct Submition
{
  double diff;
  bool is32bit;
  bool isValid;
};

static void MiningJobStop(uint32_t &job_pool, std::map<uint32_t, std::shared_ptr<Submition>> & submition_map)
{
  {
    std::lock_guard<std::mutex> lock(s_job_mutex);
    s_job_result_list.clear();
    s_job_request_list_sw.clear();
    #ifdef HARDWARE_SHA265
    s_job_request_list_hw.clear();
    #endif
  }
  s_working_current_job_id = 0xFF;
  job_pool = 0xFFFFFFFF;
  submition_map.clear();
}

#ifdef RANDOM_NONCE
uint64_t s_random_state = 1;
static uint32_t RandomGet()
{
    s_random_state += 0x9E3779B97F4A7C15ull;
    uint64_t z = s_random_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

#endif

void runStratumWorker(void *name) {

// TEST: https://bitcoin.stackexchange.com/questions/22929/full-example-data-for-scrypt-stratum-client

  Serial.println("");
  Serial.printf("\n[WORKER] Started. Running %s on core %d\n", (char *)name, xPortGetCoreID());

  #ifdef DEBUG_MEMORY
  Serial.printf("### [Total Heap / Free heap / Min free heap]: %d / %d / %d \n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
  #endif

  std::map<uint32_t, std::shared_ptr<Submition>> s_submition_map;

#ifdef I2C_SLAVE
  std::vector<uint8_t> i2c_slave_vector;

  //scan for i2c slaves
  if (i2c_master_start() == 0)
    i2c_slave_vector = i2c_master_scan(0x0, 0x80);
  Serial.printf("Found %d slave workers\n", i2c_slave_vector.size());
  if (!i2c_slave_vector.empty())
  {
    Serial.print("  Workers: ");
    for (size_t n = 0; n < i2c_slave_vector.size(); ++n)
      Serial.printf("0x%02X,", (uint32_t)i2c_slave_vector[n]);
    Serial.println("");
  }
#endif

  // connect to pool  
  double currentPoolDifficulty = DEFAULT_DIFFICULTY;
  uint32_t nonce_pool = 0;
  uint32_t job_pool = 0xFFFFFFFF;
  uint32_t last_job_time = millis();
  unsigned long auth_id = 0;
  uint32_t rejected_shares = 0;

  // Ergebnisse werden unter dem Mutex nur hierher kopiert und danach verarbeitet
  static JobResult result_batch[RESULT_QUEUE_SIZE];

  // Hashes verbuchen und ggf. Share einreichen
  auto processResult = [&](const JobResult &res)
  {
    hashes += res.nonce_count;
    (res.hw ? hashesHw : hashesSw) += res.nonce_count;
    if (res.difficulty > currentPoolDifficulty && job_pool == res.id && res.nonce != 0xFFFFFFFF && client.connected())
    {
      unsigned long sumbit_id = 0;
      tx_mining_submit(client, mWorker, mJob, res.nonce, sumbit_id);
      Serial.print("   - Current diff share: "); Serial.println(res.difficulty,12);
      Serial.print("   - Current pool diff : "); Serial.println(currentPoolDifficulty,12);
      Serial.print("   - TX SHARE: ");
      for (size_t i = 0; i < 32; i++)
          Serial.printf("%02x", res.hash[i]);
      Serial.println("");
      mLastTXtoPool = millis();

      std::shared_ptr<Submition> submition = std::make_shared<Submition>();
      submition->diff = res.difficulty;
      submition->is32bit = (res.hash[29] == 0 && res.hash[28] == 0);
      if (submition->is32bit)
      {
        submition->isValid = checkValid((unsigned char*)res.hash, mMiner.bytearray_target);
      } else
        submition->isValid = false;

      s_submition_map.insert(std::make_pair(sumbit_id, submition));
      if (s_submition_map.size() > 32)
        s_submition_map.erase(s_submition_map.begin());
    }
  };

  //Exponential backoff for pool reconnects: resume fast after a short glitch
  //(1s, 2s, 4s...) but stay gentle with the pool if it is really down (cap 15s).
  uint32_t pool_retry_delay_s = 1;

  while(true) {

    if(WiFi.status() != WL_CONNECTED){
      // WiFi is disconnected, so reconnect now
      mMonitor.NerdStatus = NM_Connecting;
      MiningJobStop(job_pool, s_submition_map);
      WiFi.reconnect();
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      continue;
    }

    if(!checkPoolConnection()){
      MiningJobStop(job_pool, s_submition_map);
      Serial.printf("Pool unreachable, retrying in %us\n", pool_retry_delay_s);
      vTaskDelay((pool_retry_delay_s * 1000) / portTICK_PERIOD_MS);
      if (pool_retry_delay_s < 15)
        pool_retry_delay_s *= 2;
      continue;
    }
    pool_retry_delay_s = 1; //connected: next incident restarts from 1s

    if(!isMinerSuscribed)
    {
      //Stop miner current jobs
      mWorker = init_mining_subscribe();

      // STEP 1: Pool server connection (SUBSCRIBE)
      if(!tx_mining_subscribe(client, mWorker)) { 
        client.stop();
        MiningJobStop(job_pool, s_submition_map);
        continue; 
      }
      
      //Bounded: both destinations are fixed arrays and the sources come straight
      //from user input in the config portal.
      snprintf(mWorker.wName, sizeof(mWorker.wName), "%s", Settings.BtcWallet);
      // Falls kein eigener Worker-Name (.name) konfiguriert ist, automatisch einen
      // eindeutigen aus der MAC-Adresse anhaengen -> jeder Miner einzeln beim Pool sichtbar.
      if (strchr(mWorker.wName, '.') == NULL) {
        char suffix[16] = ".";
        getDeviceName(suffix + 1, sizeof(suffix) - 1);
        strncat(mWorker.wName, suffix, sizeof(mWorker.wName) - strlen(mWorker.wName) - 1);
      }
      snprintf(mWorker.wPass, sizeof(mWorker.wPass), "%s", Settings.PoolPassword);
      // STEP 2: Pool authorize work (Block Info)
      // Antwort wird unten in der Empfangsschleife ausgewertet (auth_id)
      tx_mining_auth(client, mWorker.wName, mWorker.wPass, auth_id);

      // STEP 3: Suggest pool difficulty
      tx_suggest_difficulty(client, currentPoolDifficulty);

      isMinerSuscribed=true;
      uint32_t time_now = millis();
      mLastTXtoPool = time_now;
      last_job_time = time_now;
    }

    //Check if pool is down for almost 5minutes and then restart connection with pool (1min=600000ms)
    if(checkPoolInactivity(KEEPALIVE_TIME_ms, POOLINACTIVITY_TIME_ms, currentPoolDifficulty)){
      //Restart connection
      Serial.println("  Detected more than 2 min without data form stratum server. Closing socket and reopening...");
      client.stop();
      isMinerSuscribed=false;
      MiningJobStop(job_pool, s_submition_map);
      continue; 
    }

    {
      uint32_t time_now = millis();
      if (time_now < last_job_time) //32bit wrap
        last_job_time = time_now;
      if (time_now >= last_job_time + 10*60*1000)  //10minutes without job
      {
        client.stop();
        isMinerSuscribed=false;
        MiningJobStop(job_pool, s_submition_map);
        continue;
      }
    }

    uint32_t hw_midstate[8];
    uint32_t diget_mid[8];
    uint32_t bake[16];
    #if defined(CONFIG_IDF_TARGET_ESP32)
    uint8_t sha_buffer_swap[128];
    #endif

    //Read pending messages from pool
    while(client.connected() && client.available())
    {
      String line = client.readStringUntil('\n');
      //Serial.println("  Received message from pool");      
      stratum_method result = parse_mining_method(line);
      switch (result)
      {
          case MINING_NOTIFY:         if(parse_mining_notify(line, mJob))
                                      {
                                          {
                                            std::lock_guard<std::mutex> lock(s_job_mutex);
                                            s_job_request_list_sw.clear();
                                            #ifdef HARDWARE_SHA265
                                            s_job_request_list_hw.clear();
                                            #endif
                                          }
                                          //Increse templates readed
                                          templates++;
                                          job_pool++;
                                          s_working_current_job_id = job_pool & 0xFF; //Terminate current job in thread

                                          last_job_time = millis();
                                          mLastTXtoPool = last_job_time;

                                          uint32_t mh = hashes/1000000;
                                          Mhashes += mh;
                                          hashes -= mh*1000000;

                                          //Prepare data for new jobs
                                          mMiner=calculateMiningData(mWorker, mJob);

                                          memset(mMiner.bytearray_blockheader+80, 0, 128-80);
                                          mMiner.bytearray_blockheader[80] = 0x80;
                                          mMiner.bytearray_blockheader[126] = 0x02;
                                          mMiner.bytearray_blockheader[127] = 0x80;

                                          nerd_mids(diget_mid, mMiner.bytearray_blockheader);
                                          nerd_sha256_bake(diget_mid, mMiner.bytearray_blockheader+64, bake);

                                          #ifdef HARDWARE_SHA265
                                          #if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
                                            esp_sha_acquire_hardware();
                                            sha_hal_hash_block(SHA2_256,  mMiner.bytearray_blockheader, 64/4, true);
                                            sha_hal_read_digest(SHA2_256, hw_midstate);
                                            esp_sha_release_hardware();
                                          #endif
                                          #endif

                                          #if defined(CONFIG_IDF_TARGET_ESP32)
                                          for (int i = 0; i < 32; ++i)
                                            ((uint32_t*)sha_buffer_swap)[i] = __builtin_bswap32(((const uint32_t*)(mMiner.bytearray_blockheader))[i]);
                                          #endif

                                          #ifdef RANDOM_NONCE
                                          nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
                                          #else
                                            #ifdef I2C_SLAVE
                                            if (!i2c_slave_vector.empty())
                                              nonce_pool = 0x10000000;
                                            else
                                            #endif
                                              nonce_pool = 0xDA54E700;  //nonce 0x00000000 is not possible, start from some random nonce
                                          #endif
                                          

                                          {
                                            std::lock_guard<std::mutex> lock(s_job_mutex);
                                            for (int i = 0; i < 4; ++ i)
                                            {
                                              #if 1
                                              JobPush( s_job_request_list_sw, job_pool, nonce_pool, NONCE_PER_JOB_SW, currentPoolDifficulty, mMiner.bytearray_blockheader, diget_mid, bake);
                                              #ifdef RANDOM_NONCE
                                              nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
                                              #else
                                              nonce_pool += NONCE_PER_JOB_SW;
                                              #endif
                                              #endif
                                              #ifdef HARDWARE_SHA265
                                                #if defined(CONFIG_IDF_TARGET_ESP32)
                                                  JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, sha_buffer_swap, diget_mid, bake);
                                                #else
                                                  JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, mMiner.bytearray_blockheader, hw_midstate, bake);
                                                #endif
                                              #ifdef RANDOM_NONCE
                                              nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
                                              #else
                                              nonce_pool += NONCE_PER_JOB_HW;
                                              #endif
                                              #endif
                                            }
                                          }
                                          #ifdef I2C_SLAVE
                                          //Nonce for nonce_pool starts from 0x10000000
                                          //For i2c slave we give nonces from 0x20000000, that is 0x10000000 nonces per slave
                                          i2c_feed_slaves(i2c_slave_vector, job_pool & 0xFF, 0x20, currentPoolDifficulty, mMiner.bytearray_blockheader);
                                          #endif
                                      } else
                                      {
                                        Serial.println("Parsing error, need restart");
                                        client.stop();
                                        isMinerSuscribed=false;
                                        MiningJobStop(job_pool, s_submition_map);
                                      }
                                      break;
          case MINING_SET_DIFFICULTY: parse_mining_set_difficulty(line, currentPoolDifficulty);
                                      break;
          case STRATUM_SUCCESS:       {
                                        // "error":null heißt nicht zwingend akzeptiert -> "result" prüfen
                                        bool accepted = false;
                                        unsigned long id = parse_extract_id(line, accepted);
                                        if (auth_id != 0 && id == auth_id)
                                        {
                                          Serial.printf("[WORKER] Authorization %s for %s\n", accepted ? "OK" : "FAILED", mWorker.wName);
                                          auth_id = 0;
                                        }
                                        auto itt = s_submition_map.find(id);
                                        if (itt != s_submition_map.end())
                                        {
                                          if (accepted)
                                          {
                                            if (itt->second->diff > best_diff)
                                              best_diff = itt->second->diff;
                                            if (itt->second->is32bit)
                                              shares++;
                                            if (itt->second->isValid)
                                            {
                                              Serial.println("CONGRATULATIONS! Valid block found");
                                              valids++;
                                            }
                                          } else
                                          {
                                            rejected_shares++;
                                            Serial.printf("Refuse submition %lu (rejected total: %u)\n", id, rejected_shares);
                                          }
                                          s_submition_map.erase(itt);
                                        }
                                      }
                                      break;
          case STRATUM_PARSE_ERROR:   {
                                        unsigned long id = parse_extract_id(line);
                                        if (auth_id != 0 && id == auth_id)
                                        {
                                          Serial.printf("[WORKER] Authorization FAILED for %s\n", mWorker.wName);
                                          auth_id = 0;
                                        }
                                        auto itt = s_submition_map.find(id);
                                        if (itt != s_submition_map.end())
                                        {
                                          rejected_shares++;
                                          Serial.printf("Refuse submition %lu (rejected total: %u)\n", id, rejected_shares);
                                          s_submition_map.erase(itt);
                                        }
                                      }
                                      break;
          default:                    Serial.println("  Parsed JSON: unknown"); break;

      }
    }

    #ifdef I2C_SLAVE
    if (i2c_slave_vector.empty() || job_pool == 0xFFFFFFFF)
    {
      vTaskDelay(50 / portTICK_PERIOD_MS); //Small delay
    } else
    {
      uint32_t time_start = millis();
      i2c_hit_slaves(i2c_slave_vector);
      vTaskDelay(5 / portTICK_PERIOD_MS);
      uint32_t nonces_done = 0;
      std::vector<uint32_t> nonce_vector = i2c_harvest_slaves(i2c_slave_vector, job_pool & 0xFF, nonces_done);
      hashes += nonces_done;
      for (size_t n = 0; n < nonce_vector.size(); ++n)
      {
        JobResult result;
        ((uint32_t*)(mMiner.bytearray_blockheader+64+12))[0] = nonce_vector[n];
        if (nerd_sha256d_baked(diget_mid, mMiner.bytearray_blockheader+64, bake, result.hash))
        {
          result.id = job_pool;
          result.nonce = nonce_vector[n];
          result.nonce_count = 0;
          result.hw = false;
          result.difficulty = diff_from_target(result.hash);
          processResult(result);
        }
      }
      uint32_t time_end = millis();
      //if (nonces_done > 16384)
        //Serial.printf("Harvest slaves in %dms hashes=%d\n", time_end - time_start, nonces_done);
      if (time_end > time_start)
      {
        uint32_t elapsed = time_end - time_start;
        if (elapsed < 50)
          vTaskDelay((50 - elapsed) / portTICK_PERIOD_MS);
      } else
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
    #else
    vTaskDelay(50 / portTICK_PERIOD_MS); //Small delay
    #endif

    
    size_t result_count = 0;
    if (job_pool != 0xFFFFFFFF)
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      while (result_count < RESULT_QUEUE_SIZE && s_job_result_list.pop(result_batch[result_count]))
        result_count++;

#if 1
      while (!s_job_request_list_sw.full())
      {
        JobPush( s_job_request_list_sw, job_pool, nonce_pool, NONCE_PER_JOB_SW, currentPoolDifficulty, mMiner.bytearray_blockheader, diget_mid, bake);
        #ifdef RANDOM_NONCE
        nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
        #else
        nonce_pool += NONCE_PER_JOB_SW;
        #endif
      }
#endif

      #ifdef HARDWARE_SHA265
      while (!s_job_request_list_hw.full())
      {
        #if defined(CONFIG_IDF_TARGET_ESP32)
          JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, sha_buffer_swap, diget_mid, bake);
        #else
          JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, mMiner.bytearray_blockheader, hw_midstate, bake);
        #endif
        #ifdef RANDOM_NONCE
        nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
        #else
        nonce_pool += NONCE_PER_JOB_HW;
        #endif
      }
      #endif
    }

    for (size_t r = 0; r < result_count; ++r)
      processResult(result_batch[r]);
  }
}

//////////////////THREAD CALLS///////////////////

void minerWorkerSw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  Serial.printf("[MINER] %d Started minerWorkerSw Task!\n", miner_id);

  // Job wird aus der Queue kopiert, der Task arbeitet auf seiner eigenen Kopie
  JobRequest job;
  JobResult result;
  bool has_result = false;
  uint8_t hash[32];
  uint32_t wdt_counter = 0;
  while (1)
  {
    bool has_job;
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (has_result)
      {
        ResultPush(result);
        has_result = false;
      }
      has_job = s_job_request_list_sw.pop(job);
    }
    if (has_job)
    {
      result.difficulty = job.difficulty;
      result.nonce = 0xFFFFFFFF;
      result.id = job.id;
      result.nonce_count = job.nonce_count;
      result.hw = false;
      has_result = true;
      uint8_t job_in_work = job.id & 0xFF;
      for (uint32_t n = 0; n < job.nonce_count; ++n)
      {
        ((uint32_t*)(job.sha_buffer+64+12))[0] = job.nonce_start+n;
        if (nerd_sha256d_baked(job.midstate, job.sha_buffer+64, job.bake, hash))
        {
          double diff_hash = diff_from_target(hash);
          if (diff_hash > result.difficulty)
          {
            result.difficulty = diff_hash;
            result.nonce = job.nonce_start+n;
            memcpy(result.hash, hash, 32);
          }
        }

        if ( (uint16_t)(n & 0xFF) == 0 &&s_working_current_job_id != job_in_work)
        {
          result.nonce_count = n+1;
          break;
        }
      }
    } else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    wdt_counter++;
    if (wdt_counter >= 8)
    {
      wdt_counter = 0;
      esp_task_wdt_reset();
    }
  }
}

#ifdef HARDWARE_SHA265

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)

static inline IRAM_ATTR void nerd_sha_ll_fill_text_block_sha256(const void *input_text, uint32_t nonce)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    REG_WRITE(&reg_addr_buf[0], data_words[0]);
    REG_WRITE(&reg_addr_buf[1], data_words[1]);
    REG_WRITE(&reg_addr_buf[2], data_words[2]);
#if 0
    REG_WRITE(&reg_addr_buf[3], nonce);
    //REG_WRITE(&reg_addr_buf[3], data_words[3]);    
    REG_WRITE(&reg_addr_buf[4], data_words[4]);
    REG_WRITE(&reg_addr_buf[5], data_words[5]);
    REG_WRITE(&reg_addr_buf[6], data_words[6]);
    REG_WRITE(&reg_addr_buf[7], data_words[7]);
    REG_WRITE(&reg_addr_buf[8], data_words[8]);
    REG_WRITE(&reg_addr_buf[9], data_words[9]);
    REG_WRITE(&reg_addr_buf[10], data_words[10]);
    REG_WRITE(&reg_addr_buf[11], data_words[11]);
    REG_WRITE(&reg_addr_buf[12], data_words[12]);
    REG_WRITE(&reg_addr_buf[13], data_words[13]);
    REG_WRITE(&reg_addr_buf[14], data_words[14]);
    REG_WRITE(&reg_addr_buf[15], data_words[15]);
#else
    REG_WRITE(&reg_addr_buf[3], nonce);
    REG_WRITE(&reg_addr_buf[4], 0x00000080);
    REG_WRITE(&reg_addr_buf[5], 0x00000000);
    REG_WRITE(&reg_addr_buf[6], 0x00000000);
    REG_WRITE(&reg_addr_buf[7], 0x00000000);
    REG_WRITE(&reg_addr_buf[8], 0x00000000);
    REG_WRITE(&reg_addr_buf[9], 0x00000000);
    REG_WRITE(&reg_addr_buf[10], 0x00000000);
    REG_WRITE(&reg_addr_buf[11], 0x00000000);
    REG_WRITE(&reg_addr_buf[12], 0x00000000);
    REG_WRITE(&reg_addr_buf[13], 0x00000000);
    REG_WRITE(&reg_addr_buf[14], 0x00000000);
    REG_WRITE(&reg_addr_buf[15], 0x80020000);
#endif
}

static inline IRAM_ATTR void nerd_sha_ll_fill_text_block_sha256_inter()
{
  uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

  DPORT_INTERRUPT_DISABLE();
  REG_WRITE(&reg_addr_buf[0], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4));
  REG_WRITE(&reg_addr_buf[1], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4));
  REG_WRITE(&reg_addr_buf[2], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4));
  REG_WRITE(&reg_addr_buf[3], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4));
  REG_WRITE(&reg_addr_buf[4], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4));
  REG_WRITE(&reg_addr_buf[5], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4));
  REG_WRITE(&reg_addr_buf[6], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4));
  REG_WRITE(&reg_addr_buf[7], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4));
  DPORT_INTERRUPT_RESTORE();

  REG_WRITE(&reg_addr_buf[8], 0x00000080);
  REG_WRITE(&reg_addr_buf[9], 0x00000000);
  REG_WRITE(&reg_addr_buf[10], 0x00000000);
  REG_WRITE(&reg_addr_buf[11], 0x00000000);
  REG_WRITE(&reg_addr_buf[12], 0x00000000);
  REG_WRITE(&reg_addr_buf[13], 0x00000000);
  REG_WRITE(&reg_addr_buf[14], 0x00000000);
  REG_WRITE(&reg_addr_buf[15], 0x00010000);
}

static inline IRAM_ATTR void nerd_sha_ll_read_digest(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4);  
  ((uint32_t*)ptr)[7] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);
  DPORT_INTERRUPT_RESTORE();
}


static inline IRAM_ATTR bool nerd_sha_ll_read_digest_if(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  uint32_t last = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);
  #if 1
  if ( (uint16_t)(last >> 16) != 0)
  {
    DPORT_INTERRUPT_RESTORE();
    return false;
  }
  #endif

  ((uint32_t*)ptr)[7] = last;
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4);  
  DPORT_INTERRUPT_RESTORE();
  return true;
}

static inline IRAM_ATTR void nerd_sha_ll_write_digest(void *digest_state)
{
    uint32_t *digest_state_words = (uint32_t *)digest_state;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_H_BASE);

    REG_WRITE(&reg_addr_buf[0], digest_state_words[0]);
    REG_WRITE(&reg_addr_buf[1], digest_state_words[1]);
    REG_WRITE(&reg_addr_buf[2], digest_state_words[2]);
    REG_WRITE(&reg_addr_buf[3], digest_state_words[3]);
    REG_WRITE(&reg_addr_buf[4], digest_state_words[4]);
    REG_WRITE(&reg_addr_buf[5], digest_state_words[5]);
    REG_WRITE(&reg_addr_buf[6], digest_state_words[6]);
    REG_WRITE(&reg_addr_buf[7], digest_state_words[7]);
}

static inline void nerd_sha_hal_wait_idle()
{
    while (REG_READ(SHA_BUSY_REG))
    {}
}

//#define VALIDATION
void minerWorkerHw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  Serial.printf("[MINER] %d Started minerWorkerHw Task!\n", miner_id);

  JobRequest job;
  JobResult result;
  bool has_result = false;
  uint8_t hash[32];
  uint8_t digest_mid[32];
  uint8_t sha_buffer[64];
  uint32_t wdt_counter = 0;

#ifdef VALIDATION
  uint8_t doubleHash[32];
  uint32_t diget_mid[8];
  uint32_t bake[16];
#endif

  while (1)
  {
    bool has_job;
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (has_result)
      {
        ResultPush(result);
        has_result = false;
      }
      has_job = s_job_request_list_hw.pop(job);
    }
    if (has_job)
    {
      result.id = job.id;
      result.nonce = 0xFFFFFFFF;
      result.nonce_count = job.nonce_count;
      result.hw = true;
      result.difficulty = job.difficulty;
      has_result = true;
      uint8_t job_in_work = job.id & 0xFF;
      memcpy(digest_mid, job.midstate, sizeof(digest_mid));
      memcpy(sha_buffer, job.sha_buffer+64, sizeof(sha_buffer));
#ifdef VALIDATION
      nerd_mids(diget_mid, job.sha_buffer);
      nerd_sha256_bake(diget_mid, job.sha_buffer+64, bake);
#endif

      esp_sha_acquire_hardware();
      REG_WRITE(SHA_MODE_REG, SHA2_256);
      uint32_t nend = job.nonce_start + job.nonce_count;
      for (uint32_t n = job.nonce_start; n < nend; ++n)
      {
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_write_digest(digest_mid);
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256(sha_buffer, n);
        //sha_ll_continue_block(SHA2_256);
        REG_WRITE(SHA_CONTINUE_REG, 1);
        
        sha_ll_load(SHA2_256);
        nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256_inter();
        //sha_ll_start_block(SHA2_256);
        REG_WRITE(SHA_START_REG, 1);
        sha_ll_load(SHA2_256);
        nerd_sha_hal_wait_idle();
        if (nerd_sha_ll_read_digest_if(hash))
        {
          //Serial.printf("Hw 16bit Share, nonce=0x%X\n", n);
#ifdef VALIDATION
          //Validation
          ((uint32_t*)(job.sha_buffer+64+12))[0] = n;
          nerd_sha256d_baked(diget_mid, job.sha_buffer+64, bake, doubleHash);
          for (int i = 0; i < 32; ++i)
          {
            if (hash[i] != doubleHash[i])
            {
              Serial.println("***HW sha256 esp32s3 bug detected***");
              break;
            }
          }
#endif
          //~5 per second
          double diff_hash = diff_from_target(hash);
          if (diff_hash > result.difficulty)
          {
            if (isSha256Valid(hash))
            {
              result.difficulty = diff_hash;
              result.nonce = n;
              memcpy(result.hash, hash, sizeof(hash));
            }
          }
        }
        if (
             (uint8_t)(n & 0xFF) == 0 &&
             s_working_current_job_id != job_in_work)
        {
          result.nonce_count = n-job.nonce_start+1;
          break;
        }
      }
      esp_sha_release_hardware();
    } else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    wdt_counter++;
    if (wdt_counter >= 8)
    {
      wdt_counter = 0;
      esp_task_wdt_reset();
    }
  }
}

#endif  //#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)

#if defined(CONFIG_IDF_TARGET_ESP32)

// Registerzugriffe auf die SHA-Engine. Der DPORT-Workaround der IDF ruft bei jedem Lesen eine Funktion
// auf, sperrt Interrupts und liest vorher ein APB-Register - auch in jeder Runde der Warteschleife.
// Hier nur das APB-Vorlesen, eingebettet: gemessen 470 statt 413 KH/s je Gerät, 0 Abweichungen.
// Ein trotzdem falsch gelesener Wert fiele bei der Software-Prüfung jedes Treffers auf (verifyHwHash,
// hw_errors in /info). Mit -DNERD_DPORT_SAFE wieder die IDF-Variante.
#ifndef NERD_DPORT_SAFE
// Rein direktes Lesen (auch mit memw davor) lieferte auf den CYDs bei jedem Treffer falsche Hashes.
// Das APB-Vorlesen des Workarounds bleibt daher, nur Funktionsaufruf und Interrupt-Sperre entfallen.
static inline IRAM_ATTR uint32_t nerd_dport_read(uint32_t reg)
{
  (void)*(volatile uint32_t*)0x3ff40078;  // gleiches APB-Register wie esp_dport_access_reg_read
  return *(volatile uint32_t*)reg;
}
#define NERD_REG_READ(r) nerd_dport_read(r)
#define NERD_SEQ_READ(r) nerd_dport_read(r)
#define NERD_SEQ_BEGIN()
#define NERD_SEQ_END()
#else
#define NERD_REG_READ(r) DPORT_REG_READ(r)
#define NERD_SEQ_READ(r) DPORT_SEQUENCE_REG_READ(r)
#define NERD_SEQ_BEGIN() DPORT_INTERRUPT_DISABLE()
#define NERD_SEQ_END() DPORT_INTERRUPT_RESTORE()
#endif

static inline IRAM_ATTR bool nerd_sha_ll_read_digest_swap_if(void* ptr)
{
  NERD_SEQ_BEGIN();
  uint32_t fin = NERD_SEQ_READ(SHA_TEXT_BASE + 7 * 4);
  if ( (uint32_t)(fin & 0xFFFF) != 0)
  {
    NERD_SEQ_END();
    return false;
  }
  ((uint32_t*)ptr)[7] = __builtin_bswap32(fin);
  ((uint32_t*)ptr)[0] = __builtin_bswap32(NERD_SEQ_READ(SHA_TEXT_BASE + 0 * 4));
  ((uint32_t*)ptr)[1] = __builtin_bswap32(NERD_SEQ_READ(SHA_TEXT_BASE + 1 * 4));
  ((uint32_t*)ptr)[2] = __builtin_bswap32(NERD_SEQ_READ(SHA_TEXT_BASE + 2 * 4));
  ((uint32_t*)ptr)[3] = __builtin_bswap32(NERD_SEQ_READ(SHA_TEXT_BASE + 3 * 4));
  ((uint32_t*)ptr)[4] = __builtin_bswap32(NERD_SEQ_READ(SHA_TEXT_BASE + 4 * 4));
  ((uint32_t*)ptr)[5] = __builtin_bswap32(NERD_SEQ_READ(SHA_TEXT_BASE + 5 * 4));
  ((uint32_t*)ptr)[6] = __builtin_bswap32(NERD_SEQ_READ(SHA_TEXT_BASE + 6 * 4));
  NERD_SEQ_END();
  return true;
}

static inline IRAM_ATTR void nerd_sha_ll_read_digest(void* ptr)
{
  NERD_SEQ_BEGIN();
  ((uint32_t*)ptr)[0] = NERD_SEQ_READ(SHA_TEXT_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = NERD_SEQ_READ(SHA_TEXT_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = NERD_SEQ_READ(SHA_TEXT_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = NERD_SEQ_READ(SHA_TEXT_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = NERD_SEQ_READ(SHA_TEXT_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = NERD_SEQ_READ(SHA_TEXT_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = NERD_SEQ_READ(SHA_TEXT_BASE + 6 * 4);
  ((uint32_t*)ptr)[7] = NERD_SEQ_READ(SHA_TEXT_BASE + 7 * 4);
  NERD_SEQ_END();
}

static inline IRAM_ATTR void nerd_sha_hal_wait_idle()
{
    while (NERD_REG_READ(SHA_256_BUSY_REG))
    {}
}

static inline IRAM_ATTR void nerd_sha_ll_fill_text_block_sha256(const void *input_text)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = data_words[3];
    reg_addr_buf[4]  = data_words[4];
    reg_addr_buf[5]  = data_words[5];
    reg_addr_buf[6]  = data_words[6];
    reg_addr_buf[7]  = data_words[7];
    reg_addr_buf[8]  = data_words[8];
    reg_addr_buf[9]  = data_words[9];
    reg_addr_buf[10] = data_words[10];
    reg_addr_buf[11] = data_words[11];
    reg_addr_buf[12] = data_words[12];
    reg_addr_buf[13] = data_words[13];
    reg_addr_buf[14] = data_words[14];
    reg_addr_buf[15] = data_words[15];
}

static inline IRAM_ATTR void nerd_sha_ll_fill_text_block_sha256_upper(const void *input_text, uint32_t nonce)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = __builtin_bswap32(nonce);
#if 1
    reg_addr_buf[4]  = 0x80000000;
    reg_addr_buf[5]  = 0x00000000;
    reg_addr_buf[6]  = 0x00000000;
    reg_addr_buf[7]  = 0x00000000;
    reg_addr_buf[8]  = 0x00000000;
    reg_addr_buf[9]  = 0x00000000;
    reg_addr_buf[10] = 0x00000000;
    reg_addr_buf[11] = 0x00000000;
    reg_addr_buf[12] = 0x00000000;
    reg_addr_buf[13] = 0x00000000;
    reg_addr_buf[14] = 0x00000000;
    reg_addr_buf[15] = 0x00000280;
#else
    reg_addr_buf[4]  = data_words[4];
    reg_addr_buf[5]  = data_words[5];
    reg_addr_buf[6]  = data_words[6];
    reg_addr_buf[7]  = data_words[7];
    reg_addr_buf[8]  = data_words[8];
    reg_addr_buf[9]  = data_words[9];
    reg_addr_buf[10] = data_words[10];
    reg_addr_buf[11] = data_words[11];
    reg_addr_buf[12] = data_words[12];
    reg_addr_buf[13] = data_words[13];
    reg_addr_buf[14] = data_words[14];
    reg_addr_buf[15] = data_words[15];
#endif
}

static inline IRAM_ATTR void nerd_sha_ll_fill_text_block_sha256_double()
{
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

#if 0
    //No change
    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = data_words[3];
    reg_addr_buf[4]  = data_words[4];
    reg_addr_buf[5]  = data_words[5];
    reg_addr_buf[6]  = data_words[6];
    reg_addr_buf[7]  = data_words[7];
#endif
    reg_addr_buf[8]  = 0x80000000;
    reg_addr_buf[9]  = 0x00000000;
    reg_addr_buf[10] = 0x00000000;
    reg_addr_buf[11] = 0x00000000;
    reg_addr_buf[12] = 0x00000000;
    reg_addr_buf[13] = 0x00000000;
    reg_addr_buf[14] = 0x00000000;
    reg_addr_buf[15] = 0x00000100;
}

// Nonce-Schleife in Assembler ist Standard (467 -> ~790 KH/s je CYD); -DNERD_C_LOOP = bisherige C-Schleife.
#ifndef NERD_C_LOOP
#define NERD_ASM_LOOP
#endif

#ifdef NERD_ASM_LOOP
// Nonce-Schleife komplett in Assembler, Grundlage von Gheop (PR #727, Branch all-fixes, 4d00b0b):
// - SHA_TEXT_BASE bleibt in einem Register, die Befehlsregister START/CONTINUE/LOAD/BUSY liegen bei
//   +0x90/0x94/0x98/0x9C und sind von dort per Offset erreichbar (gcc lädt jede Adresse neu).
// - Block 2 wird geschrieben, während die Engine noch Block 1 rechnet.
// - BUSY wird direkt gelesen (ohne APB-Vorlesen), nach jedem Befehl ein memw.
// Dazu hier: die obere Hälfte von Block 1 des nächsten Nonce schon während Block 3 (nach einer Wartezeit,
// siehe unten), und die Schleife liegt im Flash statt im IRAM.
// Der Nonce läuft in Big-Endian-Form mit (+0x01000000 je Schritt), daher höchstens 256 Nonces je Aufruf
// ohne Überlauf des untersten Bytes. Rückgabe: Anzahl nicht mehr gerechneter Nonces. Sobald die unteren
// 16 Bit von SHA_TEXT[7] null sind (Kandidat), endet die Schleife und der Digest steht noch in SHA_TEXT.

// Die Engine liest einen Block nicht beim START ein, sondern Wort für Wort in den ersten Runden. Die obere
// Hälfte von Block 1 des nächsten Nonce darf daher erst nach einer Wartezeit in TEXT (gemessen auf den
// CYDs: 0 nop = alle Hashes falsch, 8/16/24 nop ~10 %, ab 32 keine; 32/40/48 nop gleich schnell).
// Bringt +4,5 %, weil die Engine zwischen LOAD und dem nächsten START sonst auf 8 Schreibzugriffe wartet.
// Danach ist die Schleife am Limit der Engine: ~318 Takte je Nonce = 3 Blöcke + 2 LOADs (-DNERD_ASM_BENCH).
#ifndef NERD_ASM_PREFILL
#define NERD_ASM_PREFILL 48
#endif
#define NERD_STR2(x) #x
#define NERD_STR(x) NERD_STR2(x)
#define NERD_ASM_B1_HIGH \
      "l32i.n  a8,  %[in], 32\n\t"  "s32i.n  a8,  %[sb], 32\n\t" \
      "l32i.n  a8,  %[in], 36\n\t"  "s32i.n  a8,  %[sb], 36\n\t" \
      "l32i.n  a8,  %[in], 40\n\t"  "s32i.n  a8,  %[sb], 40\n\t" \
      "l32i.n  a8,  %[in], 44\n\t"  "s32i.n  a8,  %[sb], 44\n\t" \
      "l32i.n  a8,  %[in], 48\n\t"  "s32i.n  a8,  %[sb], 48\n\t" \
      "l32i.n  a8,  %[in], 52\n\t"  "s32i.n  a8,  %[sb], 52\n\t" \
      "l32i.n  a8,  %[in], 56\n\t"  "s32i.n  a8,  %[sb], 56\n\t" \
      "l32i.n  a8,  %[in], 60\n\t"  "s32i.n  a8,  %[sb], 60\n\t"
// Bewusst NICHT IRAM_ATTR: im IRAM konkurriert die Schleife mit dem SW-Miner (nerd_sha256d_baked, IRAM)
// auf dem anderen Kern um den Speicher. Aus dem Flash (Cache des eigenen Kerns) gemessen +7 %, HW und SW.
static uint32_t nerd_sha_nonce_run_asm(const void *in, uint32_t be_nonce0, uint32_t count)
{
  uint32_t remaining = count;
  __asm__ __volatile__(
      "mov     a13, %[n0]\n\t"
      "movi    a12, 0x01000000\n\t"
      NERD_ASM_B1_HIGH   // Wörter 8..15 von Block 1; im Durchlauf dann schon während Block 3 (unten)
  "0:\n\t"
      "l32i.n  a8,  %[in], 0\n\t"   "s32i.n  a8,  %[sb], 0\n\t"
      "l32i.n  a8,  %[in], 4\n\t"   "s32i.n  a8,  %[sb], 4\n\t"
      "l32i.n  a8,  %[in], 8\n\t"   "s32i.n  a8,  %[sb], 8\n\t"
      "l32i.n  a8,  %[in], 12\n\t"  "s32i.n  a8,  %[sb], 12\n\t"
      "l32i.n  a8,  %[in], 16\n\t"  "s32i.n  a8,  %[sb], 16\n\t"
      "l32i.n  a8,  %[in], 20\n\t"  "s32i.n  a8,  %[sb], 20\n\t"
      "l32i.n  a8,  %[in], 24\n\t"  "s32i.n  a8,  %[sb], 24\n\t"
      "l32i.n  a8,  %[in], 28\n\t"  "s32i.n  a8,  %[sb], 28\n\t"
      "movi.n  a8, 1\n\t"           "s32i    a8, %[sb], 0x90\n\t"  "memw\n\t"   // START Block 1
      // Block 2 (Headerende + Nonce + Padding), während die Engine Block 1 rechnet
      "l32i.n  a8,  %[in], 64\n\t"  "s32i.n  a8,  %[sb], 0\n\t"
      "l32i.n  a8,  %[in], 68\n\t"  "s32i.n  a8,  %[sb], 4\n\t"
      "l32i.n  a8,  %[in], 72\n\t"  "s32i.n  a8,  %[sb], 8\n\t"
      "s32i.n  a13, %[sb], 12\n\t"
      "movi    a10, 0x80000000\n\t" "s32i.n  a10, %[sb], 16\n\t"
      "movi.n  a9, 0\n\t"
      "s32i.n  a9,  %[sb], 20\n\t"
      "s32i.n  a9,  %[sb], 24\n\t"
      "s32i.n  a9,  %[sb], 28\n\t"
      "s32i.n  a9,  %[sb], 32\n\t"
      "s32i.n  a9,  %[sb], 36\n\t"
      "s32i.n  a9,  %[sb], 40\n\t"
      "s32i.n  a9,  %[sb], 44\n\t"
      "s32i.n  a9,  %[sb], 48\n\t"
      "s32i.n  a9,  %[sb], 52\n\t"
      "s32i.n  a9,  %[sb], 56\n\t"
      "movi    a11, 0x280\n\t"      "s32i.n  a11, %[sb], 60\n\t"
      "1: l32i    a8, %[sb], 0x9C\n\t"  "bnez.n  a8, 1b\n\t"
      "movi.n  a8, 1\n\t"           "s32i    a8, %[sb], 0x94\n\t"  "memw\n\t"   // CONTINUE Block 2
      "2: l32i    a8, %[sb], 0x9C\n\t"  "bnez.n  a8, 2b\n\t"
      "movi.n  a8, 1\n\t"           "s32i    a8, %[sb], 0x98\n\t"  "memw\n\t"   // LOAD Digest 1
      "movi    a11, 0x100\n\t"
      "3: l32i    a8, %[sb], 0x9C\n\t"  "bnez.n  a8, 3b\n\t"
      // zweites SHA256: Digest 1 steht in TEXT[0..7], fehlt nur das Padding (TEXT[9..14] noch 0 von Block 2)
      "s32i.n  a10, %[sb], 32\n\t"  "s32i.n  a11, %[sb], 60\n\t"
      "movi.n  a8, 1\n\t"           "s32i    a8, %[sb], 0x90\n\t"  "memw\n\t"   // START Block 3
      // Wörter 8..15 von Block 1 des nächsten Nonce schon während Block 3, nach der Wartezeit
      ".rept " NERD_STR(NERD_ASM_PREFILL) "\n\t" "nop\n\t" ".endr\n\t"
      NERD_ASM_B1_HIGH
      "add     a13, a13, a12\n\t"
      "addi    %[cnt], %[cnt], -1\n\t"
      "4: l32i    a8, %[sb], 0x9C\n\t"  "bnez.n  a8, 4b\n\t"
      "movi.n  a8, 1\n\t"           "s32i    a8, %[sb], 0x98\n\t"  "memw\n\t"   // LOAD Digest 2
      "5: l32i    a8, %[sb], 0x9C\n\t"  "bnez.n  a8, 5b\n\t"
      "l16ui   a8, %[sb], 28\n\t"
      "beqz.n  a8, 9f\n\t"
      "bnez    %[cnt], 0b\n\t"
  "9:\n\t"
      : [cnt] "+r" (remaining)
      : [sb] "r" ((uint32_t *)(SHA_TEXT_BASE)), [in] "r" (in), [n0] "r" (be_nonce0)
      : "a8", "a9", "a10", "a11", "a12", "a13", "memory");
  return remaining;
}

// Bekannter Block (Höhe 125552) einmal beim Start durch genau diese Schleife: 67 Nonces, der letzte ist
// der echte. Ergebnis in /info (hw_kat).
static void nerd_classic_kat()
{
  static const uint8_t kat[80] = {  // Header wortweise byte-getauscht wie job.sha_buffer
    0x00,0x00,0x00,0x01,0xab,0x02,0xcd,0x81,0x8b,0x9e,0x56,0x7e,0xe2,0x17,0x93,0xcd,
    0xde,0xf2,0x99,0xfe,0xb2,0x9a,0xd4,0x44,0xa4,0x1b,0x85,0xb8,0x00,0x00,0x08,0xa3,
    0x00,0x00,0x00,0x00,0xc2,0xb6,0x20,0xe3,0x75,0x8d,0xfc,0xff,0x8b,0xdb,0x23,0x04,
    0xae,0x42,0xb9,0x1e,0x1e,0x95,0x0e,0x71,0xaf,0xf7,0x97,0xd7,0xb0,0x92,0x88,0xfc,
    0x2b,0x12,0xfc,0xf1,0x4d,0xd7,0xf5,0xc7,0x1a,0x44,0xb9,0xf2,0x95,0x46,0xa1,0x42 };
  static const uint32_t want[8] = {
    0x1dbd981f,0xe6985776,0xb644b173,0xa4d0385d,0xdc1aa2a8,0x29688d1e,0x00000000,0x00000000 };
  uint8_t hdr[80] __attribute__((aligned(4)));  // aus dem RAM wie im Betrieb (Flash wäre langsamer)
  memcpy(hdr, kat, sizeof(hdr));
  nerd_sha_nonce_run_asm(hdr, __builtin_bswap32(0x9546a100), 0x43);
  uint32_t got[8];
  nerd_sha_ll_read_digest(got);
  hwKat = memcmp(got, want, sizeof(want)) == 0 ? 1 : 0;
  Serial.printf("[MINER] HW-SHA Selbsttest (Block 125552): %s\n", hwKat ? "ok" : "FEHLER");
}
#endif

#ifdef NERD_ASM_BENCH
// Diagnose (/info hw_bench): CPU-Takte für einen Engine-Block, ein LOAD, 16 Lese- und 16 Schreibzugriffe
// auf SHA_TEXT, jeweils das Minimum aus 32 Versuchen. Zeigt, wie nah die Schleife am Limit der Engine ist.
static inline uint32_t nerd_ccount()
{
  uint32_t c;
  __asm__ __volatile__("rsr.ccount %0" : "=r"(c) :: "memory");
  return c;
}
static void nerd_sha_bench()
{
  volatile uint32_t* tb = (volatile uint32_t*)SHA_TEXT_BASE;
  uint32_t blk = ~0u, load = ~0u, rd = ~0u, wr = ~0u, nonce = ~0u;
  uint8_t hdr[80] __attribute__((aligned(4))) = {0};
  for (int k = 0; k < 32; ++k)
  {
    for (int i = 0; i < 16; ++i) tb[i] = i;
    __asm__ __volatile__("memw");
    uint32_t t0 = nerd_ccount();
    tb[0x90 / 4] = 1;
    __asm__ __volatile__("memw");
    while (tb[0x9C / 4]) {}
    uint32_t t1 = nerd_ccount();
    tb[0x98 / 4] = 1;
    __asm__ __volatile__("memw");
    while (tb[0x9C / 4]) {}
    uint32_t t2 = nerd_ccount();
    uint32_t sum = 0;
    for (int i = 0; i < 16; ++i) sum += tb[8];
    uint32_t t3 = nerd_ccount();
    for (int i = 0; i < 16; ++i) tb[8] = sum;
    __asm__ __volatile__("memw");
    uint32_t t4 = nerd_ccount();
    nerd_sha_nonce_run_asm(hdr, 0, 1);
    uint32_t t5 = nerd_ccount();
    blk = std::min(blk, t1 - t0);
    load = std::min(load, t2 - t1);
    rd = std::min(rd, t3 - t2);
    wr = std::min(wr, t4 - t3);
    nonce = std::min(nonce, t5 - t4);
  }
  snprintf(hwBench, sizeof(hwBench), "blk %u load %u rd16 %u wr16 %u nonce %u",
           (unsigned)blk, (unsigned)load, (unsigned)rd, (unsigned)wr, (unsigned)nonce);
  Serial.printf("[MINER] HW-SHA Takte: %s\n", hwBench);
}
#endif

// Rechnet einen Hardware-Treffer in Software nach (ca. 6 pro Sekunde, kostet praktisch nichts).
// job.sha_buffer ist wortweise byte-getauscht (für die Engine), job.midstate ist der Software-Midstate.
static bool verifyHwHash(const JobRequest& job, uint32_t nonce, const uint8_t* hwHash)
{
  uint32_t data[4];
  const uint32_t* swapped = (const uint32_t*)(job.sha_buffer + 64);
  data[0] = __builtin_bswap32(swapped[0]);
  data[1] = __builtin_bswap32(swapped[1]);
  data[2] = __builtin_bswap32(swapped[2]);
  data[3] = nonce;
  uint8_t swHash[32];
  return nerd_sha256d_baked(job.midstate, (const uint8_t*)data, job.bake, swHash) && memcmp(swHash, hwHash, 32) == 0;
}

// Bewusst NICHT IRAM_ATTR: gemessen (A/B per WLAN, 4 Geräte) läuft die Schleife aus dem Flash-Cache
// schneller - 416 statt 344 KH/s je Gerät. Nur die Register-Hilfsfunktionen oben liegen im IRAM.
void minerWorkerHw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  Serial.printf("[MINER] %d Started minerWorkerHwEsp32D Task!\n", miner_id);

#ifdef NERD_SHA1_LOCK
  // SHA1 und SHA256 haben getrennte Engine-Sperren, teilen sich aber SHA_TEXT. Die Schleife schreibt
  // dort ohne Sperre des Speicherblocks; ein SHA1 in Hardware (WPA2-Schlüsselwechsel über mbedTLS)
  // würde es verfälschen. Mit dauerhaft belegter SHA1-Engine rechnet mbedTLS SHA1 in Software
  // (Hasenpriester, BitMaker-hub/NerdMiner_v2#826).
  esp_sha_lock_engine(SHA1);
#endif
#ifdef NERD_SHA512_LOCK
  // Dasselbe für SHA384/512 (eine gemeinsame Engine-Sperre, nur einmal belegen): TLS der HTTPS-Abrufe
  esp_sha_lock_engine(SHA2_512);
#endif

  // Job wird aus der Queue kopiert, der Task arbeitet direkt auf seiner Kopie
  JobRequest job;
  JobResult result;
  bool has_result = false;
  uint8_t hash[32];
  const uint8_t* sha_buffer = job.sha_buffer;

  while (1)
  {
    bool has_job;
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (has_result)
      {
        ResultPush(result);
        has_result = false;
      }
      has_job = s_job_request_list_hw.pop(job);
    }
    if (has_job)
    {
      result.id = job.id;
      result.nonce = 0xFFFFFFFF;
      result.nonce_count = job.nonce_count;
      result.hw = true;
      result.difficulty = job.difficulty;
      has_result = true;
      uint8_t job_in_work = job.id & 0xFF;

      esp_sha_lock_engine(SHA2_256);
#ifdef NERD_ASM_LOOP
      if (hwKat < 0)
      {
        nerd_classic_kat();
#ifdef NERD_ASM_BENCH
        nerd_sha_bench();
#endif
      }
      uint32_t n = 0;
      while (n < job.nonce_count)
      {
        // Abschnitt endet spätestens an der nächsten 256er-Grenze des Nonce (siehe nerd_sha_nonce_run_asm)
        const uint32_t base = job.nonce_start + n;
        uint32_t chunk = 256 - (base & 0xFF);
        if (chunk > job.nonce_count - n)
          chunk = job.nonce_count - n;
        n += chunk - nerd_sha_nonce_run_asm(sha_buffer, __builtin_bswap32(base), chunk);
        if (nerd_sha_ll_read_digest_swap_if(hash))
        {
          const uint32_t nonce_hit = job.nonce_start + n - 1;
          hwChecked++;
          if (!verifyHwHash(job, nonce_hit, hash))
            hwErrors++;
          else
          {
            double diff_hash = diff_from_target(hash);
            if (diff_hash > result.difficulty && isSha256Valid(hash))
            {
              result.difficulty = diff_hash;
              result.nonce = nonce_hit;
              memcpy(result.hash, hash, sizeof(hash));
            }
          }
        }
        if (s_working_current_job_id != job_in_work)
          break;
      }
      result.nonce_count = n;
#else
      for (uint32_t n = 0; n < job.nonce_count; ++n)
      {
        //((uint32_t*)(sha_buffer+64+12))[0] = __builtin_bswap32(job->nonce_start+n);

        //sha_hal_hash_block(SHA2_256, s_test_buffer, 64/4, true);
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256(sha_buffer);
        sha_ll_start_block(SHA2_256);

        //sha_hal_hash_block(SHA2_256, s_test_buffer+64, 64/4, false);
        nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256_upper(sha_buffer+64, job.nonce_start+n);
        sha_ll_continue_block(SHA2_256);

        nerd_sha_hal_wait_idle();
        sha_ll_load(SHA2_256);

        //sha_hal_hash_block(SHA2_256, interResult, 64/4, true);
        nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256_double();
        sha_ll_start_block(SHA2_256);

        nerd_sha_hal_wait_idle();
        sha_ll_load(SHA2_256);
        if (nerd_sha_ll_read_digest_swap_if(hash))
        {
          //~5 per second
          hwChecked++;
          if (!verifyHwHash(job, job.nonce_start+n, hash))
            hwErrors++;
          else
          {
            double diff_hash = diff_from_target(hash);
            if (diff_hash > result.difficulty)
            {
              if (isSha256Valid(hash))
              {
                result.difficulty = diff_hash;
                result.nonce = job.nonce_start+n;
                memcpy(result.hash, hash, sizeof(hash));
              }
            }
          }
        }
        if (
             (uint8_t)(n & 0xFF) == 0 &&
             s_working_current_job_id != job_in_work)
        {
          result.nonce_count = n+1;
          break;
        }
      }
#endif
      esp_sha_unlock_engine(SHA2_256);
    } else
    {
      hwIdle++;
      vTaskDelay(2 / portTICK_PERIOD_MS);
    }

    esp_task_wdt_reset();
  }
}

#endif  //CONFIG_IDF_TARGET_ESP32

#endif  //HARDWARE_SHA265


#define DELAY 100
#define REDRAW_EVERY 10

void restoreStat() {
  if(!Settings.saveStats) return;
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.printf("[MONITOR] NVS partition is full or has invalid version, erasing...\n");
    nvs_flash_init();
  }

  ret = nvs_open("state", NVS_READWRITE, &stat_handle);

  size_t required_size = sizeof(double);
  nvs_get_blob(stat_handle, "best_diff", &best_diff, &required_size);
  nvs_get_u32(stat_handle, "Mhashes", &Mhashes);
  uint32_t nv_shares, nv_valids;
  nvs_get_u32(stat_handle, "shares", &nv_shares);
  nvs_get_u32(stat_handle, "valids", &nv_valids);
  shares = nv_shares;
  valids = nv_valids;
  nvs_get_u32(stat_handle, "templates", &templates);
  nvs_get_u64(stat_handle, "upTime", &upTime);

  uint32_t crc = crc32_reset();
  crc = crc32_add(crc, &best_diff, sizeof(best_diff));
  crc = crc32_add(crc, &Mhashes, sizeof(Mhashes));
  crc = crc32_add(crc, &nv_shares, sizeof(nv_shares));
  crc = crc32_add(crc, &nv_valids, sizeof(nv_valids));
  crc = crc32_add(crc, &templates, sizeof(templates));
  crc = crc32_add(crc, &upTime, sizeof(upTime));
  crc = crc32_finish(crc);

  uint32_t nv_crc;
  nvs_get_u32(stat_handle, "crc32", &nv_crc);
  if (nv_crc != crc)
  {
    best_diff = 0.0;
    Mhashes = 0;
    shares = 0;
    valids = 0;
    templates = 0;
    upTime = 0;
  }
}

void saveStat() {
  if(!Settings.saveStats) return;
  Serial.printf("[MONITOR] Saving stats\n");
  nvs_set_blob(stat_handle, "best_diff", &best_diff, sizeof(best_diff));
  nvs_set_u32(stat_handle, "Mhashes", Mhashes);
  nvs_set_u32(stat_handle, "shares", shares);
  nvs_set_u32(stat_handle, "valids", valids);
  nvs_set_u32(stat_handle, "templates", templates);
  nvs_set_u64(stat_handle, "upTime", upTime);

  uint32_t crc = crc32_reset();
  crc = crc32_add(crc, &best_diff, sizeof(best_diff));
  crc = crc32_add(crc, &Mhashes, sizeof(Mhashes));
  uint32_t nv_shares = shares;
  uint32_t nv_valids = valids;
  crc = crc32_add(crc, &nv_shares, sizeof(nv_shares));
  crc = crc32_add(crc, &nv_valids, sizeof(nv_valids));
  crc = crc32_add(crc, &templates, sizeof(templates));
  crc = crc32_add(crc, &upTime, sizeof(upTime));
  crc = crc32_finish(crc);
  nvs_set_u32(stat_handle, "crc32", crc);
}

void resetStat() {
    Serial.printf("[MONITOR] Resetting NVS stats\n");
    templates = hashes = Mhashes = totalKHashes = elapsedKHs = upTime = shares = valids = 0;
    best_diff = 0.0;
    saveStat();
}

// Dauer des letzten echten Neuzeichnens in ms (/info); Durchläufe ohne Zeichnen dauern nur wenige ms
uint32_t lastDrawDurationMs = 0;

void runMonitor(void *name)
{

  Serial.println("[MONITOR] started");
  restoreStat();

  unsigned long mLastCheck = 0;

  resetToFirstScreen();

  unsigned long frame = 0;

  uint32_t seconds_elapsed = 0;

  totalKHashes = (Mhashes * 1000) + hashes / 1000;
  uint32_t last_update_millis = millis();
  uint32_t uptime_frac = 0;

  while (1)
  {
    uint32_t now_millis = millis();
    if (now_millis < last_update_millis)
      now_millis = last_update_millis;
    
    uint32_t mElapsed = now_millis - mLastCheck;
    if (mElapsed >= 1000)
    { 
      mLastCheck = now_millis;
      last_update_millis = now_millis;
      unsigned long currentKHashes = (Mhashes * 1000) + hashes / 1000;
      elapsedKHs = currentKHashes - totalKHashes;
      totalKHashes = currentKHashes;

      uptime_frac += mElapsed;
      while (uptime_frac >= 1000)
      {
        uptime_frac -= 1000;
        upTime ++;
      }

      uint32_t drawStart = millis();
      drawCurrentScreen(mElapsed);
      uint32_t drawMs = millis() - drawStart;
      if (drawMs > 20)
        lastDrawDurationMs = drawMs;

      // Monitor state when hashrate is 0.0
      if (elapsedKHs == 0)
      {
        Serial.printf(">>> [i] Miner: newJob>%s / inRun>%s) - Client: connected>%s / subscribed>%s / wificonnected>%s\n",
            "true",//(1) ? "true" : "false",
            isMinerSuscribed ? "true" : "false",
            client.connected() ? "true" : "false", isMinerSuscribed ? "true" : "false", WiFi.status() == WL_CONNECTED ? "true" : "false");
      }

      #ifdef DEBUG_MEMORY
      Serial.printf("### [Total Heap / Free heap / Min free heap]: %d / %d / %d \n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
      Serial.printf("### Max stack usage: %d\n", uxTaskGetStackHighWaterMark(NULL));
      #endif

      seconds_elapsed++;

      if(seconds_elapsed % (saveIntervals[currentIntervalIndex]) == 0){
        saveStat();
        seconds_elapsed = 0;
        if(currentIntervalIndex < saveIntervalsSize - 1)
          currentIntervalIndex++;
      }    
    }
    animateCurrentScreen(frame);
    doLedStuff(frame);

    vTaskDelay(DELAY / portTICK_PERIOD_MS);
    frame++;
  }
}
