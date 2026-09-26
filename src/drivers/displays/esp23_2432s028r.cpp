#include "displayDriver.h"

#if defined ESP32_2432S028R || ESP32_2432S028_2USB

#include <TFT_eSPI.h>
#include <TFT_eTouch.h>
// Komprimierte Fassung von images_320_170.h/images_bottom_320_70.h (tools/compress_images.py)
#include "media/images_cyd_z.h"
#include "media/myFonts.h"
#include "media/Free_Fonts.h"
#include "version.h"
#include "monitor.h"
#include "OpenFontRender.h"
#include <SPI.h>
#include "rotation.h"
#include "drivers/storage/nvMemory.h"
#include "drivers/storage/storage.h"

#define WIDTH 130 //320
#define HEIGHT 170 

extern nvMemory nvMem;

OpenFontRender render;
TFT_eSPI tft = TFT_eSPI();                  // Invoke library, pins defined in platformio.ini
TFT_eSprite background = TFT_eSprite(&tft); // Invoke library sprite
SPIClass hSPI(HSPI);
TFT_eTouch<TFT_eSPI> touch(tft, ETOUCH_CS, 0xFF, hSPI); 

extern monitor_data mMonitor;
extern pool_data pData;
extern DisplayDriver *currentDisplayDriver;
extern bool invertColors; 
extern TSettings Settings;
bool hasChangedScreen = true;

void getChipInfo(void){
  Serial.print("Chip: ");
  Serial.println(ESP.getChipModel());
  Serial.print("ChipRevision: ");
  Serial.println(ESP.getChipRevision());
  Serial.print("Psram size: ");
  Serial.print(ESP.getPsramSize() / 1024);
  Serial.println("KB");
  Serial.print("Flash size: ");
  Serial.print(ESP.getFlashChipSize() / 1024);
  Serial.println("KB");
  Serial.print("CPU frequency: ");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println("MHz");  
}

void esp32_2432S028R_Init(void)
{ 
  // getChipInfo();  
  tft.init();
  if (nvMem.loadConfig(&Settings))
    {      
     // Serial.print("Invert Colors: ");
     // Serial.println(Settings.invertColors);  
      invertColors = Settings.invertColors;           
    }  
  tft.invertDisplay(invertColors);
  tft.setRotation(1);    
  tft.setSwapBytes(true); // Swap the colour byte order when rendering
  if (invertColors) {
    tft.writecommand(ILI9341_GAMMASET);
    tft.writedata(2);
    delay(120);
    tft.writecommand(ILI9341_GAMMASET); //Gamma curve selected
    tft.writedata(1); 
  }
  hSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, ETOUCH_CS);
  touch.init();

  TFT_eTouchBase::Calibation calibation = { 233, 3785, 3731, 120, 2 };
  touch.setCalibration(calibation);

  // Configuring screen backlight brightness using ledcontrol channel 0.
  // Using 5000Hz in 8bit resolution, which gives 0-255 possible duty cycle setting.
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BL, 0);
  ledcWrite(0, Settings.Brightness);
 
  //background.createSprite(WIDTH, HEIGHT); // Background Sprite
  //background.setSwapBytes(true);
  //render.setDrawer(background);  // Link drawing object to background instance (so font will be rendered on background)
  //render.setLineSpaceRatio(0.9); // Espaciado entre texto

  // Load the font and check it can be read OK
  // if (render.loadFont(NotoSans_Bold, sizeof(NotoSans_Bold)))
  if (render.loadFont(DigitalNumbers, sizeof(DigitalNumbers)))
  {
    Serial.println("Initialise error");
    return;
  }
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(LED_PIN_B, OUTPUT);
  pinMode(LED_PIN_G, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(LED_PIN_B, HIGH);
  digitalWrite(LED_PIN_G, HIGH);
  pData.bestDifficulty = "0";
  pData.workersHash = "0";
  pData.workersCount = 0;

  // Prüft einmal beim Start, dass alle komprimierten Bilder korrekt entpackt werden (Log: "[IMG] ...")
  zImageSelfTest(cydZImages, cydZImageNames, sizeof(cydZImages) / sizeof(cydZImages[0]));
  //Serial.println("=========== Fim Display ==============") ;
}

void esp32_2432S028R_AlternateScreenState(void)
{
  Serial.println("Switching display state");
  int screen_state_duty = ledcRead(0);
  // Switching the duty cycle for the ledc channel, where the TFT_BL pin is attached.
  if (screen_state_duty > 0) {
    ledcWrite(0, 0);
  } else {
    ledcWrite(0, Settings.Brightness);
    hasChangedScreen = true; // Während "aus" wurde nichts gezeichnet -> komplett neu aufbauen
  }
}

// Nur alle DRAW_INTERVAL_ms neu zeichnen, bei Screen-Wechsel/Touch/Einschalten (hasChangedScreen)
// sofort, bei dunklem Display gar nicht. Jede Sekunde zu zeichnen kostete gemessen keine Hashrate,
// hält aber die Display-Task fast pausenlos beschäftigt (Bilder entpacken, TrueType rendern).
#ifndef DRAW_INTERVAL_ms
#define DRAW_INTERVAL_ms (5UL * 1000)
#endif
static unsigned long lastDrawMillis = 0;

// true = diesen Durchlauf nicht zeichnen (und damit auch keine API-Abfragen auslösen).
// Hashrate-Mittelwert und Statuszeile im Log laufen trotzdem jede Sekunde weiter.
static bool skipDrawing(unsigned long mElapsed)
{
  bool displayOff = ledcRead(0) == 0;
  bool drawDue = hasChangedScreen || lastDrawMillis == 0 || millis() - lastDrawMillis >= DRAW_INTERVAL_ms;
  if (!displayOff && drawDue)
  {
    lastDrawMillis = millis();
    return false;
  }
  mining_data data = getMiningData(mElapsed);
  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s%s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str(),
                displayOff ? " (display off)" : "");
  return true;
}

void esp32_2432S028R_AlternateRotation(void)
{
  tft.setRotation( flipRotation(tft.getRotation()) );
  hasChangedScreen = true;
}

bool bottomScreenBlue = true;

void printheap(){
  Serial.print("$$ Free Heap:");
  Serial.println(ESP.getFreeHeap()); 
  // Serial.printf("### stack WMark usage: %d\n", uxTaskGetStackHighWaterMark(NULL));
}

bool createBackgroundSprite(int16_t wdt, int16_t hgt){  // Set the background and link the render, used multiple times to fit in heap
  background.createSprite(wdt, hgt) ; //Background Sprite
  // printheap();
  if (background.created()) {
      background.setColorDepth(16);
      background.setSwapBytes(true);
      render.setDrawer(background); // Link drawing object to background instance (so font will be rendered on background)
      render.setLineSpaceRatio(0.9);      
  } else {
    Serial.println("#### Sprite Error ####");
    Serial.printf("Size w:%d h:%d \n", wdt, hgt);
    printheap();
  }
  return background.created();
}

extern unsigned long mPoolUpdate;

void printPoolData(){
  if ((hasChangedScreen) || (mPoolUpdate == 0) || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000)){     
      if (Settings.PoolAddress != "tn.vkbit.com") { 
          pData = getPoolData();             
          background.createSprite(320,50); //Background Sprite
          if (!background.created()) {    
            Serial.println("###### POOL SPRITE ERROR ######");
          // Serial.printf("Pool data W:%d H:%s D:%s\n", pData.workersCount, pData.workersHash, pData.bestDifficulty);
            printheap();        
          }       
          background.setSwapBytes(true);
          if (bottomScreenBlue) {
            pushImageZ(background, 0, -20, bottonPoolScreen_z);
            pushImageZ(tft, 0, 170, bottonPoolScreen_z, 20);      
          } else {
            pushImageZ(background, 0, -20, bottonPoolScreen_g_z);
            pushImageZ(tft, 0, 170, bottonPoolScreen_g_z, 20);
          }
                
          render.setDrawer(background); // Link drawing object to background instance (so font will be rendered on background)
          render.setLineSpaceRatio(1);
          
          render.setFontSize(24);
          render.cdrawString(String(pData.workersCount).c_str(), 157, 16, TFT_BLACK);
          render.setFontSize(18);
          render.setAlignment(Align::BottomRight);
          render.cdrawString(pData.workersHash.c_str(), 265, 14, TFT_BLACK);
          render.setAlignment(Align::BottomLeft);
          render.cdrawString(pData.bestDifficulty.c_str(), 54, 14, TFT_BLACK);
          background.pushSprite(0,190);      
          background.deleteSprite();
      } else {
        pData.bestDifficulty = "TESTNET";
        pData.workersHash = "TESTNET";
        pData.workersCount = 1;
        tft.fillRect(0,170,320,70, TFT_DARKGREEN);        
        background.createSprite(320,40); //Background Sprite
        background.fillSprite(TFT_DARKGREEN);
          if (!background.created()) {    
            Serial.println("###### POOL SPRITE ERROR ######");
          // Serial.printf("Pool data W:%d H:%s D:%s\n", pData.workersCount, pData.workersHash, pData.bestDifficulty);
            printheap();        
          }
        background.setFreeFont(FF24);
        background.setTextDatum(TL_DATUM);
        background.setTextSize(1);
        background.setTextColor(TFT_WHITE, TFT_DARKGREEN);        
        background.drawString("TESTNET", 50, 0, GFXFF);
        background.pushSprite(0,185);  
        mPoolUpdate = millis();
        Serial.println("Testnet");
        background.deleteSprite();
      }
  }
}



void esp32_2432S028R_MinerScreen(unsigned long mElapsed)
{
  if (skipDrawing(mElapsed)) return;

  mining_data data = getMiningData(mElapsed);

  printPoolData();

  if (hasChangedScreen) pushImageZ(tft, 0, 0, MinerScreen_z);
    
  hasChangedScreen = false; 
 
  int wdtOffset = 190;
  // Recreate sprite to the right side of the screen
  createBackgroundSprite(WIDTH-5, HEIGHT-7);
  //Print background screen    
  pushImageZ(background, -190, 0, MinerScreen_z);
  
  // Total hashes
  render.setFontSize(18);
  render.rdrawString(data.totalMHashes.c_str(), 268-wdtOffset, 138, TFT_BLACK);

  // Block templates
  render.setFontSize(18);
  render.setAlignment(Align::TopLeft);
  render.drawString(data.templates.c_str(), 189-wdtOffset, 20, 0xDEDB);
  // Best diff
  render.drawString(data.bestDiff.c_str(), 189-wdtOffset, 48, 0xDEDB);
  // 32Bit shares
  render.setFontSize(18);
  render.drawString(data.completedShares.c_str(), 189-wdtOffset, 76, 0xDEDB);
  // Hores
  render.setFontSize(14);
  render.rdrawString(data.timeMining.c_str(), 315-wdtOffset, 104, 0xDEDB);

  // Valid Blocks
  render.setFontSize(24);
  render.setAlignment(Align::TopCenter);
  render.drawString(data.valids.c_str(), 290-wdtOffset, 56, 0xDEDB);

  // Print Temp
  render.setFontSize(10);
  render.rdrawString(data.temp.c_str(), 239-wdtOffset, 1, TFT_BLACK);

  render.setFontSize(4);
  render.rdrawString(String(0).c_str(), 244-wdtOffset, 3, TFT_BLACK);

  // Print Hour
  render.setFontSize(10);
  render.rdrawString(data.currentTime.c_str(), 286-wdtOffset, 1, TFT_BLACK);

  // Push prepared background to screen
  background.pushSprite(190, 0);

  // Delete sprite to free the memory heap
  background.deleteSprite();   
  // printheap();

   //Serial.println("=========== Mining Display ==============") ;
  // Create background sprite to print data at once
  createBackgroundSprite(WIDTH-7, HEIGHT-100); // initHeight); //Background Sprite
  //Print background screen    
  pushImageZ(background, 0, -90, MinerScreen_z);

  // Hashrate 
  render.setFontSize(35);
  render.setCursor(19, 118);
  render.setFontColor(TFT_BLACK);
  render.rdrawString(data.currentHashRate.c_str(), 118, 114-90, TFT_BLACK);
  
  // Push prepared background to screen
  background.pushSprite(0, 90);
  
  // Delete sprite to free the memory heap
  background.deleteSprite();  

  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str()); 
   
  #ifdef DEBUG_MEMORY
    // Print heap
    printheap();
  #endif
}

void esp32_2432S028R_ClockScreen(unsigned long mElapsed)
{
  if (skipDrawing(mElapsed)) return;

  if (hasChangedScreen) pushImageZ(tft, 0, 0, minerClockScreen_z);
  
  printPoolData();

  hasChangedScreen = false;

  clock_data data = getClockData(mElapsed);

 // Create background sprite to print data at once
  createBackgroundSprite(270,36);

  // Print background screen
  pushImageZ(background, 0, -130, minerClockScreen_z);
  // Hashrate
  render.setFontSize(25);
  render.setFontColor(TFT_BLACK);
  render.rdrawString(data.currentHashRate.c_str(), 95, 0, TFT_BLACK);

  // Print BlockHeight
  render.setFontSize(18);
  render.rdrawString(data.blockHeight.c_str(), 254, 9, TFT_BLACK);

  // Push prepared background to screen
  background.pushSprite(0, 130);
  // Delete sprite to free the memory heap
  background.deleteSprite(); 

  createBackgroundSprite(169,105);
  // Print background screen
  pushImageZ(background, -130, -3, minerClockScreen_z);
  
  // Print BTC Price
  background.setFreeFont(FSSB9);
  background.setTextSize(1);
  background.setTextDatum(TL_DATUM);
  background.setTextColor(TFT_BLACK);
  background.drawString(data.btcPrice.c_str(), 202-130, 0, GFXFF);
 
  // Print Hour
  background.setFreeFont(FF23);
  background.setTextSize(2);
  background.setTextColor(0xDEDB, TFT_BLACK);
  background.drawString(data.currentTime.c_str(), 0, 50, GFXFF);
 
  // Push prepared background to screen
  background.pushSprite(130, 3);

  // Delete sprite to free the memory heap
  background.deleteSprite();   

  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str());

  #ifdef DEBUG_MEMORY
  // Print heap
  printheap();
  #endif
}

void esp32_2432S028R_GlobalHashScreen(unsigned long mElapsed)
{
  if (skipDrawing(mElapsed)) return;

  if (hasChangedScreen) pushImageZ(tft, 0, 0, globalHashScreen_z);
  
  printPoolData();
  
  hasChangedScreen = false;
  
  coin_data data = getCoinData(mElapsed);

  // Create background sprite to print data at once
  createBackgroundSprite(169,105);
  // Print background screen
  pushImageZ(background, -160, -3, globalHashScreen_z);
  
  // Print BTC Price
  background.setFreeFont(FSSB9);
  background.setTextSize(1);
  background.setTextDatum(TL_DATUM);
  background.setTextColor(TFT_BLACK);
  background.drawString(data.btcPrice.c_str(), 198-160, 0, GFXFF);
  // Print Hour
  background.setFreeFont(FSSB9);
  background.setTextSize(1);
  background.setTextDatum(TL_DATUM);
  background.setTextColor(TFT_BLACK);
  background.drawString(data.currentTime.c_str(), 268-160, 0, GFXFF);

  // Print Last Pool Block
  background.setFreeFont(FSS9);
  background.setTextDatum(TR_DATUM);
  background.setTextColor(0x9C92);
  background.drawString(data.halfHourFee.c_str(), 302-160, 49, GFXFF);

  // Print Difficulty
  background.setFreeFont(FSS9);
  background.setTextDatum(TR_DATUM);
  background.setTextColor(0x9C92);
  background.drawString(data.netwrokDifficulty.c_str(), 302-160, 85, GFXFF);
  // Push prepared background to screen
  background.pushSprite(160, 3);
  // Delete sprite to free the memory heap
  background.deleteSprite();   

 // Create background sprite to print data at once
  createBackgroundSprite(280,30);
  // Print background screen
  pushImageZ(background, 0, -139, globalHashScreen_z);
  //background.fillSprite(TFT_CYAN);
  // Print Global Hashrate
  render.setFontSize(17);
  render.rdrawString(data.globalHashRate.c_str(), 274, 145-139, TFT_BLACK);

  // Draw percentage rectangle
  int x2 = 2 + (138 * data.progressPercent / 100);
  background.fillRect(2, 149-139, x2, 168, 0xDEDB);

  // Print Remaining BLocks
  background.setTextFont(FONT2);
  background.setTextSize(1); 
  background.setTextDatum(MC_DATUM);
  background.setTextColor(TFT_BLACK);
  background.drawString(data.remainingBlocks.c_str(), 72, 159-139, FONT2);

  // Push prepared background to screen
  background.pushSprite(0, 139);
  // Delete sprite to free the memory heap
  background.deleteSprite();   

 // Create background sprite to print data at once
  createBackgroundSprite(140,40);
  // Print background screen
  pushImageZ(background, -5, -100, globalHashScreen_z);
  //background.fillSprite(TFT_CYAN);
  // Print BlockHeight
  render.setFontSize(28);
  render.rdrawString(data.blockHeight.c_str(), 140-5, 104-100, 0xDEDB);

  // Push prepared background to screen
  background.pushSprite(5, 100);
  // Delete sprite to free the memory heap
  background.deleteSprite();   

  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str());

  #ifdef DEBUG_MEMORY
  // Print heap
  printheap();
  #endif
}
void esp32_2432S028R_BTCprice(unsigned long mElapsed)
{
  if (skipDrawing(mElapsed)) return;

  if (hasChangedScreen) pushImageZ(tft, 0, 0, priceScreen_z);
  printPoolData();
  hasChangedScreen = false;

  clock_data data = getClockData(mElapsed);

 // Create background sprite to print data at once
  createBackgroundSprite(270,36);

  // Print background screen
  pushImageZ(background, 0, -130, priceScreen_z);
  // Hashrate
  render.setFontSize(25);
  render.setFontColor(TFT_BLACK);
  render.rdrawString(data.currentHashRate.c_str(), 95, 0, TFT_BLACK);

  // Print BlockHeight
  render.setFontSize(18);
  render.rdrawString(data.blockHeight.c_str(), 254, 9, TFT_WHITE);

  // Push prepared background to screen
  background.pushSprite(0, 130);
  // Delete sprite to free the memory heap
  background.deleteSprite(); 

  createBackgroundSprite(180,105);
  // Print background screen
  pushImageZ(background, -130, -3, priceScreen_z);
  
  // Print Hour
  background.setFreeFont(FSSB9);
  background.setTextSize(1);
  background.setTextDatum(TL_DATUM);
  background.setTextColor(TFT_BLACK);
  background.drawString(data.currentTime.c_str(), 202-130, 0, GFXFF);
 
  // Print BTC Price
  background.setFreeFont(FF24);
  background.setTextDatum(TL_DATUM);
  background.setTextSize(1);
  background.setTextColor(0xDEDB, TFT_BLACK);
  background.drawString(data.btcPrice.c_str(), 0, 50, GFXFF);
 
  // Push prepared background to screen
  background.pushSprite(130, 3);

  // Delete sprite to free the memory heap
  background.deleteSprite();   

  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str());

  #ifdef DEBUG_MEMORY
  // Print heap
  printheap();
  #endif
}

void esp32_2432S028R_LoadingScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  pushImageZ(tft, 0, 33, initScreen_z);
  tft.setTextColor(TFT_BLACK);
  tft.drawString(CURRENT_VERSION, 24, 147, FONT2);
  // delay(2000);
  // tft.fillScreen(TFT_BLACK);
  // tft.pushImage(0, 0, initWidth, initHeight, MinerScreen);
}

void esp32_2432S028R_SetupScreen(void)
{
  tft.fillScreen(TFT_BLACK);
  pushImageZ(tft, 0, 33, setupModeScreen_z);
}

void esp32_2432S028R_AnimateCurrentScreen(unsigned long frame)
{
}

// Variables para controlar el parpadeo con millis()
unsigned long previousMillis = 0;
unsigned long previousTouchMillis = 0;
char currentScreen = 0;

void esp32_2432S028R_DoLedStuff(unsigned long frame)
{
  unsigned long currentMillis = millis();    
  // / Check the touch coordinates 110x185 210x240
  if (currentMillis - previousTouchMillis >= 500)
    { 
      int16_t t_x , t_y;  // To store the touch coordinates
      bool pressed = touch.getXY(t_x, t_y);
      if (pressed) {                        
          if (((t_x > 109)&&(t_x < 211)) && ((t_y > 185)&&(t_y < 241))) {
            bottomScreenBlue ^= true;
            hasChangedScreen = true;
          } else if((t_x > 235) && ((t_y > 0)&&(t_y < 16))) {
            // Touching the top right corner of the screen, roughly in the gray status label.
            // Disabling the screen backlight. 
            esp32_2432S028R_AlternateScreenState();
          }
          else
            if (t_x > 160) {
              // next screen
             // Serial.printf("Next screen touch( x:%d y:%d )\n", t_x, t_y);              
              currentDisplayDriver->current_cyclic_screen = (currentDisplayDriver->current_cyclic_screen + 1) % currentDisplayDriver->num_cyclic_screens;
            } else if (t_x < 160)
            {
              // Previus screen
             // Serial.printf("Previus screen touch( x:%d y:%d )\n", t_x, t_y);              
              /* Serial.println(currentDisplayDriver->current_cyclic_screen); */
              currentDisplayDriver->current_cyclic_screen = currentDisplayDriver->current_cyclic_screen - 1;      
              if (currentDisplayDriver->current_cyclic_screen<0) currentDisplayDriver->current_cyclic_screen = currentDisplayDriver->num_cyclic_screens - 1;              
            }
      }
      previousTouchMillis = currentMillis;
    }

    // "= true" statt "^= true": zwei Screen-Wechsel vor dem nächsten Zeichnen hoben sich sonst auf
    if (currentScreen != currentDisplayDriver->current_cyclic_screen) hasChangedScreen = true;
    currentScreen = currentDisplayDriver->current_cyclic_screen;

  switch (mMonitor.NerdStatus)
  {
  case NM_waitingConfig:
    digitalWrite(LED_PIN, LOW); // LED encendido de forma continua
    break;

  case NM_Connecting:
    if (currentMillis - previousMillis >= 500)
    { // 0.5sec blink
      previousMillis = currentMillis;
      // Serial.print("C");
      digitalWrite(LED_PIN, HIGH);
      digitalWrite(LED_PIN_B, !digitalRead(LED_PIN)); // Cambia el estado del LED
    }
    break;

  case NM_hashing:
    if (currentMillis - previousMillis >= 500)
    { // 0.1sec blink
      // Serial.print("h");
      previousMillis = currentMillis;
      digitalWrite(LED_PIN_B, HIGH);
      digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Cambia el estado del LED      
    }
    break;
  }
  

}

CyclicScreenFunction esp32_2432S028RCyclicScreens[] = {esp32_2432S028R_MinerScreen, esp32_2432S028R_ClockScreen, esp32_2432S028R_GlobalHashScreen, esp32_2432S028R_BTCprice};

DisplayDriver esp32_2432S028RDriver = {
    esp32_2432S028R_Init,
    esp32_2432S028R_AlternateScreenState,
    esp32_2432S028R_AlternateRotation,
    esp32_2432S028R_LoadingScreen,
    esp32_2432S028R_SetupScreen,
    esp32_2432S028RCyclicScreens,
    esp32_2432S028R_AnimateCurrentScreen,
    esp32_2432S028R_DoLedStuff,
    SCREENS_ARRAY_SIZE(esp32_2432S028RCyclicScreens),
    0,
    WIDTH,
    HEIGHT};
#endif
