import argparse
import os
import sys
import freetype
import struct

# Command
# uv run convert.py Sarabun-Bold.ttf --sizes 16 20 24 --name sarabun_bold --out ./output


def char_to_utf8_int(char):
    encoded = char.encode('utf-8')
    return int.from_bytes(encoded, 'big')


def generate_c_file(font_path, size, name_base, output_dir, chars):
    face = freetype.Face(font_path)
    face.set_pixel_sizes(0, size)

    font_name = f"{name_base}{size}"
    filename = f"font_{name_base}{size}.c"
    filepath = os.path.join(output_dir, filename)

    # Collect symbols
    symbols = []

    # We need to calculate font height (max ascent + max descent generally, or from metrics)
    # smart_font usually uses global height.
    # We can iterate to find max height or use face.size.metrics
    # face.size.metrics.height is in 26.6 pixel format
    # face.size is a SizeMetrics object in some versions/contexts of the wrapper?
    # Or face.size returns the structure.
    # Based on error 'SizeMetrics' object has no attribute 'metrics', face.size IS the metrics.
    # Let's try accessing height directly on face.size if it behaves like that.
    # However, standard freetype-py: face.size is FT_Size, face.size.metrics is FT_Size_Metrics.
    # Maybe the wrapper simplifies it.
    try:
        font_height = face.size.metrics.height >> 6
    except AttributeError:
        # Fallback if face.size is the metrics object
        font_height = face.size.height >> 6
    if font_height == 0:
        # fallback if metrics are weird
        font_height = size + 2

    c_code = []

    # Header
    c_code.append(f'#include "SmartFont.h"')
    c_code.append('#if defined(__AVR__)')
    c_code.append('    #include <avr/pgmspace.h>')
    c_code.append('    #define CONST_PREFIX           const PROGMEM')
    c_code.append('#elif defined(__XTENSA__)')
    c_code.append('    #include <pgmspace.h>')
    c_code.append('    #define CONST_PREFIX           const PROGMEM')
    c_code.append('#else')
    c_code.append('    #define CONST_PREFIX           const')
    c_code.append('#endif\n')

    valid_chars = []

    for char in chars:
        # Load glyph
        # We use strict char code loading
        glyph_index = face.get_char_index(ord(char))
        if glyph_index == 0 and ord(char) != 0:
            # Skip missing glyphs, but maybe warn?
            continue

        face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER |
                        freetype.FT_LOAD_TARGET_MONO)
        bitmap = face.glyph.bitmap

        # Dimensions
        width = bitmap.width
        rows = bitmap.rows

        # Data extraction
        # FreeType bitmap buffer is row-major.
        # For mono, each byte represents 8 pixels.
        # smart_font seems to use similar 8 pixels per byte, row major?
        # Checking existing code:
        # symbol_21 (.width=1, .height=8, .data={0xfd}) -> 11111101 (binary)
        # This looks like vertical 8 pixels?
        # Wait, let's re-read smart_font internal_draw_bitmap.
        # "if ((READ_CONST_8BIT(data_ptr) & (1 << current_bit)) > 0)"
        # current_bit starts at 7, decs to 0.
        # It loops y then x.
        # So data is row-major. "current_bit" shifts for X ?.
        # No.
        # for y from y to y+h:
        #   for x from x to x+w:
        #      check bit. loop logic:
        #      if bit > 0: bit--
        #      else: bit=7, data_ptr++

        # This means the bits proceed linearly as we scan Row 0 (x=0..w), then Row 1...
        # So it IS row-major.
        # Byte 0 contains the first 8 pixels of the scan (0,0), (1,0), (2,0)...
        # If width is 5:
        # row 0: pixels 0..4 took 5 bits of Byte 0.
        # row 1: pixels 0..4 took next 5 bits?
        # Let's check logic:
        # bit starts at 7 (MSB).
        # (x, y) = (0, 0) -> check bit 7 of byte 0.
        # (x, y) = (1, 0) -> check bit 6 of byte 0.
        # ...
        # So yes, it packs pixels continuously. It does NOT align rows to byte boundaries necessarily unless width matches.

        data_bytes = []
        current_byte = 0
        current_bit = 7

        # We need to iterate pixels in the bitmap
        # FreeType Mono bitmap:
        # pitch is bytes per row.
        # buffer is array.

        ft_buffer = bitmap.buffer
        ft_pitch = bitmap.pitch

        # Extract packed bits for our format
        packed_data = []
        pack_byte = 0
        pack_bit = 7

        for y in range(rows):
            src_row_start = y * ft_pitch
            for x in range(width):
                # Check pixel (x,y) in FreeType buffer
                # FT stores MSB first in byte.
                byte_idx = src_row_start + (x >> 3)
                bit_idx = 7 - (x & 7)

                pixel_on = False
                if byte_idx < len(ft_buffer):
                    if (ft_buffer[byte_idx] & (1 << bit_idx)):
                        pixel_on = True

                if pixel_on:
                    pack_byte |= (1 << pack_bit)

                pack_bit -= 1
                if pack_bit < 0:
                    packed_data.append(pack_byte)
                    pack_byte = 0
                    pack_bit = 7

        # Append remaining byte if partially filled
        if pack_bit != 7:
            packed_data.append(pack_byte)

        # Metrics
        offset_x = face.glyph.bitmap_left
        # Invert Y for typical coord systems if needed, but smart_font seems to assume top-left origin?
        offset_y = -face.glyph.bitmap_top
        # smart_font text drawing:
        # y loop: current_y - font->height to current_y ... wait
        # smart_font_print:
        #   offset_y calculation...
        #   internal_draw_bitmap(..., inst->current_y + offset_y, ...)
        # It seems smart_font draws relative to a baseline or reference point.
        # In existing code: "symbol_21 ... offset_y = -8".
        # If font height is 16.
        # It generally draws upwards from baseline?
        # Let's trust FreeType for now. bitmap_top is distance from baseline upwards.
        # If we use coordinate system where +y is down.
        # Baseline might be at Y=height.
        # A char like 'A' (height 8) sits on baseline. bitmap_top=8.
        # We want to draw at (cursor_x, cursor_y).
        # If cursor_y is baseline.
        # We should draw at cursor_y - bitmap_top.
        # So offset_y = -bitmap_top seems correct.

        # Advance
        cur_dist = face.glyph.advance.x >> 6

        valid_chars.append({
            'utf8_int': char_to_utf8_int(char),
            'width': width,
            'height': rows,
            'data': packed_data,
            'char': char,
            'offset_x': offset_x,
            'offset_y': offset_y,
            'cur_dist': cur_dist
        })

    # Generate C structs
    for idx, item in enumerate(valid_chars):
        utf8_hex = f"0x{item['utf8_int']:x}"
        var_name = f"symbol_{utf8_hex}"

        c_code.append(f"static CONST_PREFIX smart_font_bitmap_t {var_name} = {{")
        c_code.append(f"    .width = {item['width']},")
        c_code.append(f"    .height = {item['height']},")
        c_code.append("    .data = {")

        # Data bytes
        d_str = ""
        for b in item['data']:
            d_str += f"0x{b:02x}, "
        c_code.append(f"        {d_str}")
        c_code.append("    }")
        c_code.append("};\n")

    # Generate Info Struct
    c_code.append(f"const smart_font_info_t font_{font_name} = {{")
    c_code.append(f"    .count = {len(valid_chars)},")
    c_code.append(f"    .font_size = {size},")
    c_code.append(f"    .height = {font_height},")
    c_code.append("    .symbols = {")

    for item in valid_chars:
        utf8_hex = f"0x{item['utf8_int']:x}"
        var_name = f"symbol_{utf8_hex}"
        c_code.append(
            f"        {{.utf8=0x{item['utf8_int']:x}, .offset_x={item['offset_x']}, .offset_y={item['offset_y']}, .cur_dist={item['cur_dist']}, .bitmap=&{var_name}}},")

    c_code.append("    }")
    c_code.append("};")

    with open(filepath, 'w', encoding='utf-8') as f:
        f.write('\n'.join(c_code))

    print(f"Generated {filepath}")


def main():
    parser = argparse.ArgumentParser(
        description="Convert Fonts to SmartFont C files")
    parser.add_argument("font", help="Path to TTF/OTF font file")
    parser.add_argument("--sizes", nargs='+', type=int,
                        required=True, help="List of sizes to generate")
    parser.add_argument("--name", required=True,
                        help="Base variable name for the font")
    parser.add_argument("--out", default=".", help="Output directory")

    args = parser.parse_args()

    # Character Range
    # ASCII
    chars = [chr(i) for i in range(0x20, 0x7F)]
    # Thai
    chars += [chr(i) for i in range(0x0E00, 0x0E7F + 1)]

    # Sanitize and prepare output dir
    if not os.path.exists(args.out):
        os.makedirs(args.out)

    for size in args.sizes:
        generate_c_file(args.font, size, args.name, args.out, chars)


if __name__ == "__main__":
    main()
