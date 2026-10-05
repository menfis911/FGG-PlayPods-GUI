# FGG-PlayPods-GUI
#
#   export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
#   make

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    ifeq ($(filter check test clean,$(MAKECMDGOALS)),)
        $(error PS5_PAYLOAD_SDK is undefined)
    endif
endif

ELF   := fgg-playpods-gui.elf
BUILD := build
VERSION := $(shell cat VERSION)
WEB_ASSETS := $(BUILD)/generated/web_assets.c

CFLAGS     := -std=c11 -Wall -Wextra -Werror -O2 -Isrc -Ithird_party/sbc \
              -DFGG_VERSION='"$(VERSION)"'
LDFLAGS    := -L$(PS5_PAYLOAD_SDK)/target/lib
LDLIBS     := -lpthread
# Vendored code is built as upstream wrote it, without this project's
# warning policy.
SBC_CFLAGS := -std=gnu11 -O2 -w -Ithird_party/sbc

SRCS     := src/main.c src/backend.c src/http_server.c src/capture.c src/hci.c \
            src/bt.c src/sdp.c src/a2dp.c src/log.c $(WEB_ASSETS)
SBC_SRCS := third_party/sbc/sbc.c third_party/sbc/sbc_primitives.c
OBJS     := $(patsubst %.c,$(BUILD)/%.o,$(SRCS) $(SBC_SRCS))

.PHONY: all clean test check deploy

all: $(ELF)

$(ELF): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(WEB_ASSETS): web/index.html web/styles.css web/app.js tools/embed_assets.py
	python3 tools/embed_assets.py . $@

$(BUILD)/third_party/%.o: third_party/%.c
	@mkdir -p $(dir $@)
	$(CC) $(SBC_CFLAGS) -c -o $@ $<

$(BUILD)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/$(BUILD)/generated/%.o: $(BUILD)/generated/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

check:
	python3 -m unittest discover -s tests -v
	node --check web/app.js
	node --check homebrew.js

test: check

deploy: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^

clean:
	rm -rf $(BUILD) $(ELF)
