// Bisher nur vom CYD-Treiber genutzt; der ROM-Header esp32/rom/miniz.h gilt nur für den klassischen ESP32
#if defined(ESP32_2432S028R) || defined(ESP32_2432S028_2USB)

#include <Arduino.h>
#include <esp32/rom/miniz.h>
#include "zImage.h"
#include "utils.h"

// Muss zum Fenster in tools/compress_images.py passen (WINDOW_BITS = 12)
#define ZIMAGE_DICT_SIZE 4096

bool zImageDecode(const ZImage& img, int32_t firstRow, int32_t lastRow, ZRowFunc fn, void* ctx)
{
  const size_t row_bytes = (size_t)img.width * 2;
  // Decoder-Zustand (~11 KB), Wörterbuch und Zeilenpuffer nur während des Entpackens belegen
  tinfl_decompressor* inflator = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));
  uint8_t* dict = (uint8_t*)malloc(ZIMAGE_DICT_SIZE);
  uint16_t* row = (uint16_t*)malloc(row_bytes);
  if (!inflator || !dict || !row)
  {
    Serial.printf("[IMG] Zu wenig RAM zum Entpacken (frei: %u)\n", ESP.getFreeHeap());
    free(inflator);
    free(dict);
    free(row);
    return false;
  }

  tinfl_init(inflator);
  size_t in_ofs = 0;
  size_t dict_ofs = 0;
  size_t row_fill = 0;
  int32_t row_idx = 0;
  bool ok = false;
  for (;;)
  {
    size_t in_bytes = img.size - in_ofs;
    size_t out_bytes = ZIMAGE_DICT_SIZE - dict_ofs;
    // Wörterbuch als Ringpuffer: out_buf_start = dict, Größe = Zweierpotenz
    tinfl_status status = tinfl_decompress(inflator, img.data + in_ofs, &in_bytes,
                                           dict, dict + dict_ofs, &out_bytes, TINFL_FLAG_PARSE_ZLIB_HEADER);
    in_ofs += in_bytes;

    // Neu erzeugte Bytes liegen in dict[dict_ofs .. dict_ofs + out_bytes) und werden zu Zeilen zusammengesetzt
    const uint8_t* out = dict + dict_ofs;
    size_t avail = out_bytes;
    while (avail > 0 && row_idx <= lastRow)
    {
      size_t take = row_bytes - row_fill;
      if (take > avail)
        take = avail;
      memcpy((uint8_t*)row + row_fill, out, take);
      row_fill += take;
      out += take;
      avail -= take;
      if (row_fill == row_bytes)
      {
        if (row_idx >= firstRow)
          fn(ctx, row_idx, row);
        row_idx++;
        row_fill = 0;
      }
    }
    dict_ofs = (dict_ofs + out_bytes) & (ZIMAGE_DICT_SIZE - 1);

    if (row_idx > lastRow)
    {
      ok = true;  // alle benötigten Zeilen gezeichnet, Rest nicht mehr entpacken
      break;
    }
    if (status != TINFL_STATUS_HAS_MORE_OUTPUT)
    {
      if (status != TINFL_STATUS_DONE)
        Serial.printf("[IMG] Entpacken fehlgeschlagen (Status %d)\n", (int)status);
      break;      // DONE vor lastRow = Daten zu kurz, sonst Fehler
    }
  }

  free(inflator);
  free(dict);
  free(row);
  return ok;
}

struct CrcCtx
{
  uint32_t crc;
  int32_t rows;
  uint16_t width;
};

bool zImageSelfTest(const ZImage* const* images, const char* const* names, size_t count)
{
  size_t good = 0;
  for (size_t i = 0; i < count; ++i)
  {
    const ZImage& img = *images[i];
    CrcCtx c = { crc32_reset(), 0, img.width };
    bool decoded = zImageDecode(img, 0, img.height - 1, [](void* p, int32_t, uint16_t* pixels) {
      CrcCtx* c = (CrcCtx*)p;
      c->crc = crc32_add(c->crc, pixels, (size_t)c->width * 2);
      c->rows++;
    }, &c);
    uint32_t crc = crc32_finish(c.crc);
    if (decoded && c.rows == img.height && crc == img.crc32)
      good++;
    else
      Serial.printf("[IMG] %s FEHLER: %d Zeilen, CRC %08X statt %08X\n", names[i], (int)c.rows, crc, img.crc32);
  }
  Serial.printf("[IMG] Selbsttest: %u/%u Bilder OK\n", (unsigned)good, (unsigned)count);
  return good == count;
}

#endif
