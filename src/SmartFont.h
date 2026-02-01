/**
 * SmartFont Library
 */

#ifndef SMART_FONT_H
#define SMART_FONT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t width;
  uint16_t height;
  uint8_t data[];
} smart_font_bitmap_t;

typedef struct {
  uint32_t utf8;
  int16_t offset_x;
  int16_t offset_y;
  uint16_t cur_dist;
  const smart_font_bitmap_t *bitmap;
} smart_font_symbol_t;

typedef struct {
  uint16_t count;
  uint16_t font_size;
  uint16_t height;
  const smart_font_symbol_t *symbols;
  
} smart_font_info_t_header; 

typedef struct {
  uint16_t count;
  uint16_t font_size;
  uint16_t height;
  smart_font_symbol_t symbols[];
} smart_font_info_t;

typedef void (*SmartFontDrawPixelCb)(int16_t x, int16_t y);
typedef void (*SmartFontClearPixelCb)(int16_t x, int16_t y);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class SmartFont {
public:
  SmartFont(SmartFontDrawPixelCb drawCb, SmartFontClearPixelCb clearCb);

  void setFont(const smart_font_info_t *font);
  void setPos(int16_t x, int16_t y);
  void print(const char *str);
  int16_t getWidth(const char *str);

  // Additional methods if needed
  void setResolution(uint16_t x, uint16_t y);

private:
  SmartFontDrawPixelCb _drawPixel;
  SmartFontClearPixelCb _clearPixel;
  const smart_font_info_t *_font;

  int16_t _currentX;
  int16_t _currentY;
  uint16_t _resX;
  uint16_t _resY;

  // Helper functions
  uint8_t findSymbol(const char *str, const smart_font_symbol_t **output);
  void drawBitmap(int16_t x, int16_t y, const smart_font_bitmap_t *bitmap);

  // Thai language specific helpers
  uint8_t isUtf8(const char *str);
  uint8_t getUtf8Format(const char *str, uint32_t *data);

  bool isOverheadLv1(const smart_font_symbol_t *symbol);
  bool isOverheadLv2(const smart_font_symbol_t *symbol);
  bool isPadding(const smart_font_symbol_t *symbol);
  bool isUnder(const smart_font_symbol_t *symbol);
  bool shouldPadding(const smart_font_symbol_t *prev, const smart_font_symbol_t *curr,
                     const smart_font_symbol_t *next);
};
#endif

#endif // SMART_FONT_H
