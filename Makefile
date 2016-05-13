TARGET = avr-proxy

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

ELF = $(BUILD_DIR)/$(TARGET)
FLASH_0 = $(ELF)-0x00000.bin
FLASH_4 = $(ELF)-0x40000.bin

VPATH = $(SRC_DIR)

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ	= $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))

SDK_DIR = /Volumes/case-sensitive/esp-open-sdk/sdk

CC = xtensa-lx106-elf-gcc
AR = xtensa-lx106-elf-ar
LD = xtensa-lx106-elf-gcc
CFLAGS = -std=gnu99 -I. -I$(INC_DIR) -Ilibesphttpd/include -Werror -Wpointer-arith -Wundef -Wall -mlongcalls -DUSE_OPENSDK -DWIFI_SSID=\"${WIFI_SSID}\" -DWIFI_PASS=\"${WIFI_PASS}\" -Wl,-EL -fno-inline-functions -mtext-section-literals -Wno-address -Werror -Wpointer-arith

LDLIBS = -nostdlib -Wl,-no-check-sections -Wl,-static -Wl,--start-group -lc -lgcc -lmain -lnet80211 -lwpa -llwip -lpp -lphy -lhal -lcrypto -lesphttpd -lwebpages-espfs -Wl,--end-group
LDFLAGS = -Teagle.app.v6.ld -Llibesphttpd -L $(SDK_DIR)/lib

.PHONY: clean flash debug checkdirs all libesphttpd

# debug:
# 	$(info $$OBJ is [${OBJ}])

all: checkdirs $(FLASH_0)

libesphttpd/Makefile:
	$(Q) echo "No libesphttpd submodule found. Using git to fetch it..."
	$(Q) git submodule init
	$(Q) git submodule update

libesphttpd: libesphttpd/Makefile
	find html -name '*~' -delete
	$(Q) make -C libesphttpd USE_OPENSDK=yes

$(FLASH_0): $(ELF)
	esptool.py elf2image $^

$(ELF): libesphttpd $(OBJ)
	$(LD) $(LDFLAGS) $(OBJ) $(LDLIBS) -o $@

checkdirs: $(BUILD_DIR)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

flash: $(FLASH_0)
	esptool.py write_flash 0 $(FLASH_0) 0x40000 $(FLASH_4)

clean:
	make -C libesphttpd clean
	rm -f $(BUILD_DIR)/*
