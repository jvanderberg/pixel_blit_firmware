#!/bin/bash
# Build the debug parser tool

cd "$(dirname "$0")"

gcc -o debug_parser debug_parser.c \
    ../src/board_config.c \
    ../lib/fseq_parser/src/fseq_parser.c \
    -I../src -I../lib/fseq_parser/include -I../lib/pb_led_driver \
    -DBOARD_CONFIG_TEST_BUILD -DPB_LED_DRIVER_TEST_BUILD \
    -Wall -Wextra -g

if [ $? -eq 0 ]; then
    echo "Build successful: ./debug_parser"
    echo ""
    echo "Usage: ./debug_parser [board_id]"
    echo "  Place config.csv and test.fseq in the debug/ directory"
else
    echo "Build failed"
    exit 1
fi
