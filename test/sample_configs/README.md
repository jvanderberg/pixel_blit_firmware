# Sample config.csv files

Copy any of these to the SD card root as `config.csv` to test different configurations.

**Note:** These files are validated by unit tests in `test/board_config/test_board_config.c`

## Files

- `config_single_board_3_strings.csv` - 3 strings: 50, 50, 100 pixels
- `config_single_board_gaps.csv` - 8 strings with gap (0-2 active, 3-5 disabled, 6-7 active)
- `config_two_boards.csv` - Board 0: 4 strings @ 50px, Board 1: 3 strings @ 100px
- `config_full_32_strings.csv` - All 32 strings with mixed color orders
- `config_malformed.csv` - Invalid line for error testing (line 2 has "not_a_number")

## Format

Each line is: `pixel_count,color_order`

- `pixel_count`: Number of pixels (0 = disabled)
- `color_order`: RGB, GRB, BGR, RBG, GBR, or BRG

Row N corresponds to string N. Board M reads rows `M*32` to `M*32+31`.

**Important:** Comments and blank lines count as rows, so don't use them if you want row numbers to match string numbers.
