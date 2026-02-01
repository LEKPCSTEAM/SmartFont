# SmartFont TTF to C Converter

A tool to convert TTF/OTF font files into C source files compatible with the `SmartFont` library.

## Prerequisites

- [uv](https://github.com/astral-sh/uv) (Recommended for dependency management)
- Python 3.x
- `freetype-py`

## Installation

```bash
uv sync
```

## Usage

```bash
uv run convert.py [FONT_FILE] --sizes [SIZES...] --name [VAR_NAME] --out ./output
```

### Arguments

- `font`: Path to the input `.ttf` or `.otf` file.
- `--sizes`: List of font sizes to generate (e.g., `16 20 24`).
- `--name`: Base name for the generated C variables (e.g., `font_sarabun_bold`).
- `--out`: Output directory for the `.c` files.

## Example

```bash
uv run convert.py MyFont.ttf --sizes 16 20 24 --name font_my_font --out ../../../src/fonts
```

This will generate files like `font_my_font16.c`, `font_my_font20.c`, etc.
