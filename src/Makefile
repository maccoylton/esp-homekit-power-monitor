PROGRAM = main

EXTRA_COMPONENTS = \
	extras/dht \
    extras/dhcpserver \
	extras/http-parser \
	extras/rboot-ota \
	$(abspath ../components/esp-wolfssl) \
	$(abspath ../components/esp-cjson) \
	$(abspath ../components/esp-homekit) \
    $(abspath ../components/esp-adv-button) \
    $(abspath ../components/UDPlogger)\
    $(abspath ../components/esp-wifi-config) \
    $(abspath ../components/esp-homekit-common-functions/led_codes)\
    $(abspath ../components/esp-homekit-common-functions/ota)\
    $(abspath ../components/esp-homekit-common-functions/custom_characteristics)\
    $(abspath ../components/esp-homekit-common-functions/shared_functions)\
    $(abspath ../components/esp-homekit-eve-history/src)



FLASH_SIZE ?= 8
FLASH_MODE ?= dout
FLASH_SPEED ?= 40

HOMEKIT_MAX_CLIENTS = 16
HOMEKIT_SMALL = 0

HOMEKIT_SPI_FLASH_BASE_ADDR ?= 0x8c000

EXTRA_CFLAGS += -I../.. -DHOMEKIT_SHORT_APPLE_UUIDS
EXTRA_CFLAGS += -DHOMEKIT_DEBUG
EXTRA_CFLAGS += -DHOMEKIT_OVERCLOCK_PAIR_VERIFY
EXTRA_CFLAGS += -DHOMEKIT_OVERCLOCK_PAIR_SETUP
EXTRA_CFLAGS += -DUDPLOG_PRINTF_TO_UDP
EXTRA_CFLAGS += -DUDPLOG_PRINTF_ALSO_SERIAL
EXTRA_CFLAGS += -DconfigUSE_TRACE_FACILITY

include $(SDK_PATH)/common.mk

signature:
	$(shell /opt/local/bin/openssl   sha384 -binary -out firmware/main.bin.sig firmware/main.bin)
	$(shell printf "%08x" `cat firmware/main.bin | wc -c`| xxd -r -p >>firmware/main.bin.sig)
 
monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud 115200 --elf $(PROGRAM_OUT)
