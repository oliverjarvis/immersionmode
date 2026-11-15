CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
NM      := $(CROSS)nm
AR      := $(CROSS)ar
BUILD   ?= ../build/payload
GEN     ?= ../build/gen
DEBUG   ?= 0
BEARSSL ?= ../build/cache/bearssl-0.6

PAYLOAD_BASE := 0x0FE70000
PAYLOAD_SIZE := 0x00090000
STAGE_BASE   := 0x006A4000
STAGE_SIZE   := 0x00030000
STUB_BASE    := 0x005B99F8
ENTRY_TARGET ?= 0x00100024
ENTRY_RETURN ?= 0x00100004

ARCH    := -march=armv6k -mtune=mpcore -marm -mfloat-abi=soft
CFLAGS  := $(ARCH) -std=c11 -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns \
           -MMD -MP -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Isrc -Isrc/libc -I$(BEARSSL)/inc -I$(GEN) -DJPR_DEBUG=$(DEBUG)
LDFLAGS := -nostdlib -Wl,--gc-sections -Wl,-T,link.ld -Wl,--build-id=none

C_SRCS := src/libc/string.c src/core/capture.c src/core/util.c src/core/surface.c src/core/font.c src/core/draw.c src/core/annot.c src/core/card.c src/core/config.c src/core/url.c src/core/kokoro.c src/core/word.c src/core/outbox.c src/core/sentence.c src/core/json.c src/core/http.c src/core/grammar.c src/core/status.c src/core/layout.c src/core/panel.c src/core/reply.c src/core/replies.c src/core/ipc.c src/core/voice.c src/core/mix.c src/core/menu.c src/core/screen.c src/core/strings.c \
          src/platform/init.c src/platform/handlers.c src/platform/hook_rt.c src/platform/debug.c \
          src/platform/display.c src/platform/fs.c src/platform/data.c src/platform/net.c src/platform/tls.c src/platform/worker.c src/platform/kokoro.c src/platform/snd.c src/platform/voice.c src/platform/mix.c src/platform/choices.c src/platform/peek.c src/platform/screen.c
S_SRCS := src/platform/svc.s src/platform/mix_asm.s
OBJS   := $(C_SRCS:%.c=$(BUILD)/%.o) $(S_SRCS:%.s=$(BUILD)/%.o) $(BUILD)/hooks_gen.o

.PHONY: all host-test
all: $(BUILD)/payload.bin $(BUILD)/stub.bin $(BUILD)/symbols.txt

$(BUILD)/%.o: %.c $(wildcard $(GEN)/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -c $< -o $@

$(BUILD)/hooks_gen.o: $(GEN)/hooks_gen.s
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -c $< -o $@

BEARSSL_SRCS := $(shell find $(BEARSSL)/src -name '*.c' 2>/dev/null)
BEARSSL_OBJS := $(BEARSSL_SRCS:$(BEARSSL)/src/%.c=$(BUILD)/bearssl/%.o)
BEARSSL_CFLAGS := $(ARCH) -Os -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns \
                  -ffunction-sections -fdata-sections -Isrc/libc -I$(BEARSSL)/inc -I$(BEARSSL)/src \
                  -DBR_USE_URANDOM=0 -DBR_USE_WIN32_RAND=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 \
                  -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_POWER8=0 -DBR_INT128=0 -DBR_UMUL128=0

$(BUILD)/bearssl/%.o: $(BEARSSL)/src/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(BEARSSL_CFLAGS) -c $< -o $@

$(BUILD)/libbearssl.a: $(BEARSSL_OBJS)
	@rm -f $@
	$(AR) rcs $@ $^

$(BUILD)/payload.elf: $(OBJS) $(BUILD)/libbearssl.a link.ld
	$(CC) $(ARCH) $(LDFLAGS) $(OBJS) $(BUILD)/libbearssl.a -lgcc -o $@

$(BUILD)/payload.bin: $(BUILD)/payload.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/symbols.txt: $(BUILD)/payload.elf
	$(NM) $< > $@

$(BUILD)/stub.elf: src/platform/stub.s Makefile
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) -nostdlib -Wl,--build-id=none -Wl,-e,jpr_stub -Wl,-Ttext=$(STUB_BASE) \
	  -Wa,--defsym,PAYLOAD_BASE=$(PAYLOAD_BASE) -Wa,--defsym,PAYLOAD_SIZE=$(PAYLOAD_SIZE) \
	  -Wa,--defsym,STAGE_BASE=$(STAGE_BASE) -Wa,--defsym,STAGE_SIZE=$(STAGE_SIZE) \
	  -Wa,--defsym,ENTRY_TARGET=$(ENTRY_TARGET) -Wa,--defsym,ENTRY_RETURN=$(ENTRY_RETURN) \
	  $< -o $@

$(BUILD)/stub.bin: $(BUILD)/stub.elf
	$(OBJCOPY) -O binary -j .text $< $@

# Rebuild objects when a header they include changes.
-include $(C_SRCS:%.c=$(BUILD)/%.d)

HOST_CC := cc -std=c11 -Wall -Wextra -Werror -Isrc -DJPR_HOST=1

host-test:
	@mkdir -p $(BUILD)
	$(HOST_CC) host_tests/test_capture.c src/core/capture.c src/core/util.c -o $(BUILD)/test_capture
	$(BUILD)/test_capture
	$(HOST_CC) host_tests/test_surface.c src/core/surface.c -o $(BUILD)/test_surface
	$(BUILD)/test_surface
	$(HOST_CC) -Ihost_tests host_tests/test_font.c src/core/font.c -o $(BUILD)/test_font
	$(BUILD)/test_font
	$(HOST_CC) -Ihost_tests host_tests/test_draw.c src/core/draw.c src/core/font.c src/core/surface.c -o $(BUILD)/test_draw
	$(BUILD)/test_draw
	$(HOST_CC) host_tests/test_layout.c src/core/layout.c -o $(BUILD)/test_layout
	$(BUILD)/test_layout
	$(HOST_CC) -Ihost_tests host_tests/test_panel.c src/core/panel.c src/core/reply.c src/core/layout.c src/core/font.c src/core/surface.c src/core/draw.c src/core/util.c src/core/annot.c src/core/card.c src/core/sentence.c src/core/grammar.c src/core/json.c src/core/kokoro.c src/core/word.c src/core/mix.c src/core/screen.c -o $(BUILD)/test_panel
	$(BUILD)/test_panel
	$(HOST_CC) host_tests/test_ipc.c src/core/ipc.c -o $(BUILD)/test_ipc
	$(BUILD)/test_ipc
	$(HOST_CC) host_tests/test_util.c src/core/util.c -o $(BUILD)/test_util
	$(BUILD)/test_util
	$(HOST_CC) host_tests/test_annot.c src/core/annot.c -o $(BUILD)/test_annot
	$(BUILD)/test_annot
	$(HOST_CC) host_tests/test_card.c src/core/card.c -o $(BUILD)/test_card
	$(BUILD)/test_card
	$(HOST_CC) -Isrc host_tests/test_string.c -o $(BUILD)/test_string
	$(BUILD)/test_string
	$(HOST_CC) host_tests/test_config.c src/core/config.c -o $(BUILD)/test_config
	$(BUILD)/test_config
	$(HOST_CC) host_tests/test_url.c src/core/url.c -o $(BUILD)/test_url
	$(BUILD)/test_url
	$(HOST_CC) host_tests/test_kokoro.c src/core/kokoro.c -o $(BUILD)/test_kokoro
	$(BUILD)/test_kokoro
	$(HOST_CC) host_tests/test_word.c src/core/word.c src/core/card.c -o $(BUILD)/test_word
	$(BUILD)/test_word
	$(HOST_CC) host_tests/test_outbox.c src/core/outbox.c src/core/util.c -o $(BUILD)/test_outbox
	$(BUILD)/test_outbox
	$(HOST_CC) host_tests/test_voice.c src/core/voice.c -o $(BUILD)/test_voice
	$(BUILD)/test_voice
	$(HOST_CC) host_tests/test_mix.c src/core/mix.c -o $(BUILD)/test_mix
	$(BUILD)/test_mix
	$(HOST_CC) host_tests/test_menu.c src/core/menu.c src/core/util.c -o $(BUILD)/test_menu
	$(BUILD)/test_menu
	$(HOST_CC) host_tests/test_screen.c src/core/screen.c src/core/layout.c src/core/util.c -o $(BUILD)/test_screen
	$(BUILD)/test_screen
	$(HOST_CC) host_tests/test_strings.c src/core/strings.c -o $(BUILD)/test_strings
	$(BUILD)/test_strings
	$(HOST_CC) host_tests/test_sentence.c src/core/sentence.c src/core/layout.c src/core/util.c -o $(BUILD)/test_sentence
	$(BUILD)/test_sentence
	$(HOST_CC) host_tests/test_json.c src/core/json.c src/core/util.c -o $(BUILD)/test_json
	$(BUILD)/test_json
	$(HOST_CC) host_tests/test_http.c src/core/http.c src/core/util.c -o $(BUILD)/test_http
	$(BUILD)/test_http
	$(HOST_CC) host_tests/test_grammar.c src/core/grammar.c src/core/json.c src/core/util.c -o $(BUILD)/test_grammar
	$(BUILD)/test_grammar
	$(HOST_CC) host_tests/test_replies.c src/core/replies.c src/core/grammar.c src/core/json.c src/core/util.c -o $(BUILD)/test_replies
	$(BUILD)/test_replies
	$(HOST_CC) host_tests/test_reply.c src/core/reply.c -o $(BUILD)/test_reply
	$(BUILD)/test_reply
	$(HOST_CC) host_tests/test_status.c src/core/status.c src/core/util.c -o $(BUILD)/test_status
	$(BUILD)/test_status
