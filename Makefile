# hermes-radio-daemon – Hamlib-based HERMES radio control daemon
#
# Copyright (C) 2024-2025 Rhizomatica
# Author: Rafael Diniz <rafael@riseup.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later

CC      = gcc
OBJCOPY ?= objcopy
CFLAGS  = -Ofast -Wall -std=gnu11 -fstack-protector \
          -I. -I/usr/include/iniparser -Iinclude
LDFLAGS = -liniparser -lhamlib -lasound -lcrypto -lfftw3f -lpthread -lm

include vendor/radev2/sources.mk
include vendor/radev1/sources.mk
include vendor/ft8_lib/sources.mk
include vendor/minimodem/sources.mk

# Detect architecture for tuning flags
uname_p := $(shell uname -m)
ifeq ($(uname_p),aarch64)
	CFLAGS += -moutline-atomics -march=armv8-a+crc
else
	CFLAGS += -march=x86-64-v2
endif

.PHONY: all clean install sbitx_controller test compat-tests

all: radio_daemon radio_client

TEST_CFLAGS = -O0 -Wall -Wextra -std=gnu11 -fstack-protector \
              -I. -I/usr/include/iniparser -Iinclude
TEST_BINS = tests/backend_selection_test tests/compat_surface_test

# ── daemon ──────────────────────────────────────────────────────
DAEMON_OBJS = radio_daemon.o \
              radio_backend.o \
              hamlib/radio_daemon_core.o \
              hamlib/radio_pipeline.o \
              sbitx_bootstrap.o \
              hamlib/radio_hamlib.o \
              hamlib/radio_media.o  \
              radio_shm.o    \
              radio_websocket.o \
              cfg_utils.o    \
              shm_utils.o    \
              sbitx/embedded_prefixed.o
RADIO_DAEMON_LDFLAGS = $(LDFLAGS) -li2c -lssl -lcsdr -lfftw3 -lspecbleach -lcw

radio_daemon: $(DAEMON_OBJS)
	$(CC) -o radio_daemon $(DAEMON_OBJS) $(RADIO_DAEMON_LDFLAGS)

radio_daemon.o: radio_daemon.c radio.h radio_backend.h
	$(CC) -c $(CFLAGS) radio_daemon.c -o radio_daemon.o

hamlib/radio_daemon_core.o: hamlib/radio_daemon_core.c hamlib/radio_daemon_core.h radio_backend.h radio.h \
                      hamlib/radio_media.h hamlib/radio_pipeline.h radio_shm.h radio_websocket.h cfg_utils.h
	$(CC) -c $(CFLAGS) hamlib/radio_daemon_core.c -o hamlib/radio_daemon_core.o

radio_backend.o: radio_backend.c radio_backend.h hamlib/radio_daemon_core.h hamlib/radio_hamlib.h \
                  sbitx_bootstrap.h cfg_utils.h
	$(CC) -c $(CFLAGS) radio_backend.c -o radio_backend.o

hamlib/radio_pipeline.o: hamlib/radio_pipeline.c hamlib/radio_pipeline.h radio.h
	$(CC) -c $(CFLAGS) hamlib/radio_pipeline.c -o hamlib/radio_pipeline.o

sbitx_bootstrap.o: sbitx_bootstrap.c sbitx_bootstrap.h radio.h
	$(CC) -c $(CFLAGS) sbitx_bootstrap.c -o sbitx_bootstrap.o

hamlib/radio_hamlib.o: hamlib/radio_hamlib.c hamlib/radio_hamlib.h radio.h cfg_utils.h hamlib/radio_pipeline.h
	$(CC) -c $(CFLAGS) hamlib/radio_hamlib.c -o hamlib/radio_hamlib.o

hamlib/radio_media.o: hamlib/radio_media.c hamlib/radio_media.h radio.h hamlib/radio_pipeline.h
	$(CC) -c $(CFLAGS) hamlib/radio_media.c -o hamlib/radio_media.o

radio_shm.o: radio_shm.c radio_shm.h radio.h radio_backend.h \
              include/sbitx_io.h include/radio_cmds.h shm_utils.h
	$(CC) -c $(CFLAGS) radio_shm.c -o radio_shm.o

radio_websocket.o: radio_websocket.c radio_websocket.h radio.h \
                     radio_backend.h hamlib/radio_media.h hamlib/radio_pipeline.h
	$(CC) -c $(CFLAGS) radio_websocket.c -o radio_websocket.o

cfg_utils.o: cfg_utils.c cfg_utils.h radio.h
	$(CC) -c $(CFLAGS) cfg_utils.c -o cfg_utils.o

shm_utils.o: shm_utils.c shm_utils.h
	$(CC) -c $(CFLAGS) shm_utils.c -o shm_utils.o

# ── client ──────────────────────────────────────────────────────
radio_client: sbitx_client.c sbitx_io.c shm_utils.c help.h \
              include/sbitx_io.h include/radio_cmds.h
	$(CC) $(CFLAGS) sbitx_client.c sbitx_io.c shm_utils.c \
	      -o radio_client -lpthread

# ── sBitx embedded controller ───────────────────────────────────
SBITX_CFLAGS = $(CFLAGS) -I. -Isbitx -Idsp -Isbitx/gpiolib -I/usr/include/csdr \
               $(RADEV2_EMBED_CPPFLAGS) $(RADEV2_EMBED_CFLAGS) \
               $(FT8_LIB_CPPFLAGS) $(FT8_LIB_CFLAGS) \
               $(MM_FSK_CPPFLAGS) $(MM_FSK_CFLAGS) \
               -Wno-deprecated-declarations
SBITX_LDFLAGS = -liniparser -li2c -lssl -lcrypto -lpthread -lasound -lm -lfftw3 -lcsdr -lspecbleach -lcw
SBITX_HDRS = $(wildcard sbitx/*.h dsp/*.h sbitx/gpiolib/*.h include/*.h \
               vendor/radev2/src/*.h vendor/radev2/support/*.h) shm_utils.h
SBITX_GPIOLIB_SRCS = sbitx/gpiolib/gpiolib.c \
                     sbitx/gpiolib/gpiochip_bcm2712.c \
                     sbitx/gpiolib/gpiochip_bcm2835.c \
                     sbitx/gpiolib/gpiochip_rp1.c \
                     sbitx/gpiolib/util.c
SBITX_RADAE_SRCS = $(RADEV2_EMBED_SRCS)
SBITX_FT8_SRCS = $(FT8_LIB_SRCS)
SBITX_MM_SRCS = $(MM_FSK_SRCS)
SBITX_SRCS = sbitx/sbitx_controller.c \
             sbitx/sbitx_alsa.c \
             sbitx/sbitx_buffer.c \
             sbitx/sbitx_bridge.c \
             sbitx/sbitx_core.c \
             dsp/sbitx_dsp.c \
             sbitx/sbitx_gpio.c \
             sbitx/sbitx_i2c.c \
             dsp/sbitx_radae.c \
             dsp/sbitx_drm.c \
             dsp/sbitx_ft8.c \
             dsp/sbitx_cw.c \
             dsp/sbitx_rtty.c \
             sbitx/sbitx_shm.c \
             sbitx/sbitx_si5351.c \
             sbitx/sbitx_websocket.c \
             sbitx/cfg_utils.c \
             sbitx/mongoose.c \
             sbitx/ring_buffer.c \
             shm_utils.c \
             $(SBITX_RADAE_SRCS) \
             $(SBITX_FT8_SRCS) \
             $(SBITX_MM_SRCS)
SBITX_EMBED_RAW = sbitx/embedded_raw.o
SBITX_EMBED_MAP = sbitx/embedded.syms
SBITX_EMBED_OBJ = sbitx/embedded_prefixed.o

$(SBITX_EMBED_OBJ): $(SBITX_SRCS) $(SBITX_GPIOLIB_SRCS) $(SBITX_HDRS)
	$(CC) -r $(SBITX_CFLAGS) $(SBITX_SRCS) $(SBITX_GPIOLIB_SRCS) -o $(SBITX_EMBED_RAW)
	nm -g --defined-only $(SBITX_EMBED_RAW) | awk '$$3 != "" { print $$3 " sbitx_" $$3; }' > $(SBITX_EMBED_MAP)
	$(OBJCOPY) --redefine-syms=$(SBITX_EMBED_MAP) $(SBITX_EMBED_RAW) $(SBITX_EMBED_OBJ)

sbitx_controller: $(SBITX_SRCS) $(SBITX_GPIOLIB_SRCS) include/sbitx_io.h include/radio_cmds.h
	$(CC) $(SBITX_CFLAGS) $(SBITX_SRCS) $(SBITX_GPIOLIB_SRCS) -o sbitx_controller $(SBITX_LDFLAGS)

# ── regression tests ───────────────────────────────────────────────
test: compat-tests

compat-tests: $(TEST_BINS)
	./tests/backend_selection_test
	./tests/compat_surface_test

tests/backend_selection_test: tests/backend_selection_test.c cfg_utils.c cfg_utils.h \
                              radio_backend.c radio_backend.h hamlib/radio_daemon_core.h \
                              hamlib/radio_hamlib.h sbitx_bootstrap.h radio.h \
                              tests/fixtures/backend-default.ini \
                              tests/fixtures/backend-zbitx.ini
	$(CC) $(TEST_CFLAGS) tests/backend_selection_test.c -o $@ -liniparser -lpthread

tests/compat_surface_test: tests/compat_surface_test.c radio_shm.c radio_shm.h \
                           radio_websocket.c radio_websocket.h hamlib/radio_pipeline.c \
                           hamlib/radio_pipeline.h radio_backend.h hamlib/radio_media.h radio.h \
                           shm_utils.h include/sbitx_io.h include/radio_cmds.h
	$(CC) $(TEST_CFLAGS) tests/compat_surface_test.c -o $@ -lcrypto -lpthread

# ── install ─────────────────────────────────────────────────────
prefix     ?= /usr
sysconfdir ?= /etc

install: radio_daemon radio_client
	install -D -m 755 radio_daemon  $(DESTDIR)$(prefix)/bin/radio_daemon
	install -D -m 755 radio_client  $(DESTDIR)$(prefix)/bin/radio_client
	ln -sf radio_client $(DESTDIR)$(prefix)/bin/sbitx_client
	install -d $(DESTDIR)$(sysconfdir)/sbitx
	test -f $(DESTDIR)$(sysconfdir)/sbitx/core.ini || \
	  install -m 644 config/core.ini $(DESTDIR)$(sysconfdir)/sbitx/core.ini
	test -f $(DESTDIR)$(sysconfdir)/sbitx/user.ini || \
	  install -m 644 config/user.ini $(DESTDIR)$(sysconfdir)/sbitx/user.ini

# ── clean ───────────────────────────────────────────────────────
clean:
	rm -f radio_daemon radio_client sbitx_controller $(DAEMON_OBJS) \
	      $(SBITX_EMBED_RAW) $(SBITX_EMBED_MAP) $(SBITX_EMBED_OBJ) \
	      $(TEST_BINS)
