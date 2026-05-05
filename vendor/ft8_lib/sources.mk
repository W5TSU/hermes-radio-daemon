FT8_LIB_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

FT8_LIB_CPPFLAGS += -I$(FT8_LIB_DIR) -I$(FT8_LIB_DIR)/ft8 -I$(FT8_LIB_DIR)/common -I$(FT8_LIB_DIR)/fft
FT8_LIB_CFLAGS +=
FT8_LIB_LDFLAGS += -lm
FT8_LIB_SRCS += \
$(FT8_LIB_DIR)/ft8/encode.c \
$(FT8_LIB_DIR)/ft8/decode.c \
$(FT8_LIB_DIR)/ft8/message.c \
$(FT8_LIB_DIR)/ft8/text.c \
$(FT8_LIB_DIR)/ft8/crc.c \
$(FT8_LIB_DIR)/ft8/ldpc.c \
$(FT8_LIB_DIR)/ft8/constants.c \
$(FT8_LIB_DIR)/common/monitor.c \
$(FT8_LIB_DIR)/common/wave.c \
$(FT8_LIB_DIR)/fft/kiss_fft.c \
$(FT8_LIB_DIR)/fft/kiss_fftr.c
