RADEV1_EMBED_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

RADEV1_EMBED_CPPFLAGS += -I$(RADEV1_EMBED_DIR)/src
RADEV1_EMBED_CFLAGS +=
RADEV1_EMBED_LDFLAGS += -lm
RADEV1_EMBED_SRCS += \
$(RADEV1_EMBED_DIR)/src/rade_api_nopy.c \
$(RADEV1_EMBED_DIR)/src/rade_tx.c \
$(RADEV1_EMBED_DIR)/src/rade_rx.c \
$(RADEV1_EMBED_DIR)/src/rade_enc.c \
$(RADEV1_EMBED_DIR)/src/rade_enc_data.c \
$(RADEV1_EMBED_DIR)/src/rade_dec.c \
$(RADEV1_EMBED_DIR)/src/rade_dec_data.c \
$(RADEV1_EMBED_DIR)/src/rade_dsp.c \
$(RADEV1_EMBED_DIR)/src/rade_ofdm.c \
$(RADEV1_EMBED_DIR)/src/rade_bpf.c \
$(RADEV1_EMBED_DIR)/src/rade_acq.c
