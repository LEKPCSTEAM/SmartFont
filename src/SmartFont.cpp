/**
 * SmartFont Library Implementation
 */

#include "SmartFont.h"
#include <stdlib.h>
#include <string.h>

// Platform handling
#if defined(__AVR__)
#include <avr/pgmspace.h>
#define READ_CONST_16BIT(x) pgm_read_word(x)
#define READ_CONST_8BIT(x) pgm_read_byte(x)
#elif defined(__XTENSA__) || defined(ESP32) || defined(ESP8266)
#include <pgmspace.h>
#define READ_CONST_16BIT(x) pgm_read_word(x)
#define READ_CONST_8BIT(x) pgm_read_byte(x)
#else
#define READ_CONST_16BIT(x) (*(const uint16_t *)(x))
#define READ_CONST_8BIT(x) (*(const uint8_t *)(x))
#endif

// Thai Character Constants
#define UNDER_SYMBOL_LEN 3
static const uint32_t under_symbol[] = {0xe0b8b8, 0xe0b8b9, 0xe0b8ba};

#define OVERHEAD_LV1_SYMBOL_LEN 6
static const uint32_t overhead_lv1_symbol[] = {0xe0b8b1, 0xe0b8b4, 0xe0b8b5,
                                               0xe0b8b6, 0xe0b8b7, 0xe0b987};

#define OVERHEAD_LV2_SYMBOL_LEN 7
static const uint32_t overhead_lv2_symbol[] = {
    0xe0b988, 0xe0b989, 0xe0b98a, 0xe0b98b, 0xe0b98c, 0xe0b98d, 0xe0b98e};

#define PADDING_SYMBOL_LEN 1
static const uint32_t padding_symbol[] = {0xe0b8b3};

// Comparison function for binary search
static int symbol_compare(const void *key, const void *element) {
  const uint32_t *search_key = (const uint32_t *)key;
  const smart_font_symbol_t *symbol = (const smart_font_symbol_t *)element;

  if (*search_key < symbol->utf8)
    return -1;
  if (*search_key > symbol->utf8)
    return 1;
  return 0;
}

SmartFont::SmartFont(SmartFontDrawPixelCb drawCb, SmartFontClearPixelCb clearCb)
    : _drawPixel(drawCb), _clearPixel(clearCb), _font(NULL), _currentX(0),
      _currentY(0), _resX(10000), _resY(10000) // Default large res if not set
{}

void SmartFont::setFont(const smart_font_info_t *font) { _font = font; }

void SmartFont::setPos(int16_t x, int16_t y) {
  _currentX = x;
  _currentY = y;
}

void SmartFont::setResolution(uint16_t x, uint16_t y) {
  _resX = x;
  _resY = y;
}

uint8_t SmartFont::isUtf8(const char *str) { return (*str & 0x80) > 0; }

uint8_t SmartFont::getUtf8Format(const char *str, uint32_t *data) {
  uint32_t val = 0;
  unsigned char utf_data =
      (unsigned char)*str; // Use unsigned char for bitwise ops
  uint8_t size = 0;

  if (utf_data & 0x80) {
    do {
      val <<= 8;
      val |= 0x000000ff & (uint32_t)*str++;
      utf_data <<= 1;
      size++;
    } while (utf_data & 0x80);
  } else {
    val = utf_data;
    size = 1;
  }

  *data = val;
  return size;
}

uint8_t SmartFont::findSymbol(const char *str,
                              const smart_font_symbol_t **output) {
  uint32_t data = 0;
  uint8_t size = 0;

  if (isUtf8(str)) {
    size = getUtf8Format(str, &data);
  } else {
    data = ((uint32_t)*str & 0x000000ff);
    size = 1;
  }

  if (_font) {
    // Using bsearch from stdlib
    *output = (const smart_font_symbol_t *)bsearch(
        &data, _font->symbols, _font->count, sizeof(smart_font_symbol_t),
        symbol_compare);
  } else {
    *output = NULL;
  }

  return size;
}

void SmartFont::drawBitmap(int16_t x, int16_t y,
                           const smart_font_bitmap_t *bitmap) {
  int16_t current_x;
  int16_t current_y;
  uint8_t current_bit = 7;

  // In original code data_ptr assumes data is flexible array member directly
  const uint8_t *data_ptr = bitmap->data;

  uint16_t h = READ_CONST_16BIT(&bitmap->height);
  uint16_t w = READ_CONST_16BIT(&bitmap->width);

  for (current_y = y; current_y < (y + h); current_y++) {
    for (current_x = x; current_x < (x + w); current_x++) {
      if ((READ_CONST_8BIT(data_ptr) & (1 << current_bit)) > 0) {
        if (_drawPixel)
          _drawPixel(current_x, current_y);
      }

      if (current_bit > 0) {
        current_bit--;
      } else {
        current_bit = 7;
        data_ptr++;
      }
    }
  }
}

// Thai Helpers
bool SmartFont::isOverheadLv1(const smart_font_symbol_t *symbol) {
  for (int i = 0; i < OVERHEAD_LV1_SYMBOL_LEN; i++) {
    if (overhead_lv1_symbol[i] == symbol->utf8)
      return true;
  }
  return false;
}

bool SmartFont::isOverheadLv2(const smart_font_symbol_t *symbol) {
  for (int i = 0; i < OVERHEAD_LV2_SYMBOL_LEN; i++) {
    if (overhead_lv2_symbol[i] == symbol->utf8)
      return true;
  }
  return false;
}

bool SmartFont::isPadding(const smart_font_symbol_t *symbol) {
  for (int i = 0; i < PADDING_SYMBOL_LEN; i++) {
    if (padding_symbol[i] == symbol->utf8)
      return true;
  }
  return false;
}

bool SmartFont::shouldPadding(const smart_font_symbol_t *prev,
                              const smart_font_symbol_t *curr,
                              const smart_font_symbol_t *next) {
  if (prev && curr && next) {
    if (isOverheadLv2(curr) && (isOverheadLv1(prev) || isPadding(next))) {
      return true;
    }
  } else if (prev && curr) {
    if (isOverheadLv2(curr) && isOverheadLv1(prev)) {
      return true;
    }
  }
  return false;
}

void SmartFont::print(const char *str) {
  if (!_font)
    return;

  const char *current_str = str;
  uint8_t size = 0;
  uint8_t next_size = 0;
  int16_t offset_y = 0;
  int16_t x, y;

  int16_t start_x = _currentX;
  int16_t start_y = _currentY;

  const smart_font_symbol_t *current_symbol = NULL;
  const smart_font_symbol_t *prev_symbol = NULL;
  const smart_font_symbol_t *next_symbol = NULL;

  // First Pass: Clear Area
  while (*current_str != '\0') {
    size = findSymbol(current_str, &current_symbol);
    current_str += size;
    if (current_symbol != NULL) {
      if (_clearPixel) {
        for (y = (_currentY - _font->height); y < (_currentY); y++) {
          for (x = (_currentX + current_symbol->offset_x);
               x < (_currentX + current_symbol->cur_dist); x++) {
            _clearPixel(x, y);
          }
        }
      }
      _currentX += current_symbol->cur_dist;
    }
  }

  // Restore Position
  _currentX = start_x;
  _currentY = start_y;
  current_str = str;

  // Second Pass: Draw
  while (*current_str != '\0') {
    if (next_symbol == NULL) {
      size = findSymbol(current_str, &current_symbol);
    } else {
      current_symbol = next_symbol;
      size = next_size;
    }

    current_str += size;

    if (current_symbol != NULL) {
      if ((_currentX + READ_CONST_16BIT(&current_symbol->bitmap->width)) >=
          _resX) {
        // Newline logic could go here
      }

      next_size = findSymbol(current_str, &next_symbol);

      if (isOverheadLv2(current_symbol)) {
        if (shouldPadding(prev_symbol, current_symbol, next_symbol)) {
          offset_y = current_symbol->offset_y;
        } else {
          offset_y = current_symbol->offset_y +
                     READ_CONST_16BIT(&current_symbol->bitmap->height);
        }
      } else {
        offset_y = current_symbol->offset_y;
      }

      drawBitmap(_currentX + current_symbol->offset_x, _currentY + offset_y,
                 current_symbol->bitmap);

      _currentX += current_symbol->cur_dist;
      prev_symbol = current_symbol;
    }
  }
}

int16_t SmartFont::getWidth(const char *str) {
  const char *current_str = str;
  uint8_t size = 0;
  int16_t width = 0;
  const smart_font_symbol_t *current_symbol = NULL;

  while (*current_str != '\0') {
    size = findSymbol(current_str, &current_symbol);
    current_str += size;
    if (current_symbol != NULL) {
      width += current_symbol->cur_dist;
    }
  }
  return width;
}
