#ifndef _ZIMAGE_H_
#define _ZIMAGE_H_

// zlib-komprimierte RGB565-Bilder (erzeugt von tools/compress_images.py), zeilenweise entpackt
// mit dem tinfl-Decoder aus dem ESP32-ROM. Spart gegenüber unkomprimierten Bildern ~700 KB Flash.

#include <stdint.h>

struct ZImage
{
  uint16_t width;
  uint16_t height;
  uint32_t size;        // Länge der komprimierten Daten
  const uint8_t* data;  // zlib-Stream (4-KB-Fenster)
  uint32_t crc32;       // CRC32 der entpackten Pixel (Selbsttest)
};

typedef void (*ZRowFunc)(void* ctx, int32_t row, uint16_t* pixels);

// Entpackt die Zeilen 0..lastRow und ruft fn für jede Zeile ab firstRow auf.
// false bei Speichermangel oder defekten Daten.
bool zImageDecode(const ZImage& img, int32_t firstRow, int32_t lastRow, ZRowFunc fn, void* ctx);

// Entpackt alle Bilder und vergleicht die CRC32; Ergebnis im Log. true, wenn alle stimmen.
bool zImageSelfTest(const ZImage* const* images, const char* const* names, size_t count);

// Wie dst.pushImage(x, y, img.width, rows, pixels) für TFT_eSPI und TFT_eSprite.
// Zeilen außerhalb des Ziels werden nicht gezeichnet, nach der letzten sichtbaren wird abgebrochen.
template <class T>
bool pushImageZ(T& dst, int32_t x, int32_t y, const ZImage& img, int32_t rows = -1)
{
  int32_t h = (rows < 0 || rows > img.height) ? img.height : rows;
  int32_t first = y < 0 ? -y : 0;
  int32_t last = dst.height() - y - 1;
  if (last > h - 1)
    last = h - 1;
  if (last < first)
    return true;

  struct Ctx { T* dst; int32_t x; int32_t y; int32_t w; } ctx = { &dst, x, y, img.width };
  return zImageDecode(img, first, last, [](void* p, int32_t row, uint16_t* pixels) {
    Ctx* c = (Ctx*)p;
    c->dst->pushImage(c->x, c->y + row, c->w, 1, pixels);
  }, &ctx);
}

#endif
