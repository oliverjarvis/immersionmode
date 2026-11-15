"""Addresses and sizes fixed by the Ultra Moon v1.0 executable and by the spec."""

CODE_BASE = 0x00100000
TEXT_SIZE = 0x004B99F8
CODE_FILE_SIZE = 0x005A4000
STUB_BASE = CODE_BASE + TEXT_SIZE          # 0x005B99F8: unused tail of the text segment
STUB_MAX = 0x608                           # stub plus hook veneers
IMAGE_END = 0x006D4000                     # the game maps CRO modules from here up; never extend past it
STAGE_BASE = 0x006A4000                    # payload image travels in the original bss pages
STAGE_MAX = IMAGE_END - STAGE_BASE         # 192 KB for code, rodata and data
KOKORO_ADDR = 0x0FE10000                   # kokoro's word list and marks (stage 8B), below the panel image
KOKORO_SIZE = 0x00010000
PANEL_IMAGE_ADDR = 0x0FE20000              # the panel's own image, redrawn only when it changes
PANEL_IMAGE_SIZE = 0x00050000              # 320 KB: one bottom screen in RGBA8, rounded up to pages
PAYLOAD_BASE = 0x0FE70000                  # where the stub copies the payload at start-up (heap region)
PAYLOAD_SIZE = 0x00090000                  # 576 KB budget, image plus bss
PAYLOAD_END = PAYLOAD_BASE + PAYLOAD_SIZE  # 0x0FF00000, the socket buffer starts here
SOCK_BUF_ADDR = 0x0FF00000
SOCK_BUF_SIZE = 0x00060000
LANG_BYTE = 0x0066FA10                     # current message language: 0 kana, 1 kanji, 2 English
TITLE_ID = 0x00040000001B5100
TITLE_ID_STR = "00040000001B5100"
