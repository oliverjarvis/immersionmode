#ifndef JPR_GAME_H
#define JPR_GAME_H

/* Offsets inside Ultra Moon v1.0 objects, read from the disassembly. */
#define MSGDATA_HEADER    0x14 /* const u8*: text file header */
#define TEXT_HDR_LINES    0x02 /* u16 inside that header */
#define TEXT_HDR_TOTAL    0x04 /* u32 inside that header */
#define TALK_BUF_RAW      0xA0 /* StrBuf*: line as fetched */
#define TALK_BUF_EXPANDED 0xA4 /* StrBuf*: line after name substitution */
#define TALK_WORDSET      0xAC /* WordSet*: null when the window substitutes nothing */
#define STRBUF_PTR        0x04 /* const u16*: text */
#define STRBUF_LEN        0x0A /* u16: length in UTF-16 units */

/* System. */
#define CFGMEM_APPMEMALLOC 0x1FF80040 /* u32: size of the application memory region */
#define JPR_SOCK_BUF_ADDR  0x0FF00000
#define JPR_SOCK_BUF_SIZE  0x00060000
#define JPR_PANEL_IMAGE_ADDR 0x0FE20000 /* the panel's own image, copied to the screen each frame */
#define JPR_PANEL_IMAGE_SIZE 0x00050000
#define JPR_KOKORO_ADDR 0x0FE10000 /* kokoro's word list and marks */
#define JPR_KOKORO_SIZE 0x00010000

/* A talk window's message window (print::MessageWindow, a gfl2::str::StrWin). */
#define TALK_G2D 0x60     /* app::util::G2DUtil* */
#define G2D_MSGWIN 0x34   /* print::MessageWindow* */
#define MSGWIN_STATE 0x2D /* u8: 2 while it waits for a button press */
#define MSGWIN_TAG 0x68   /* u16: the tag it stopped at (0xBE00 scroll, 0xBE01 clear) */

/* Display and language. */
#define DISPLAY_BOTTOM 0x401
#define BOTTOM_WIDTH 320
#define LANG_BYTE 0x0066FA10u /* u8: message language; 0 kana, 1 kanji, 2 English */

#endif
