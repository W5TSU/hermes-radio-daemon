MM_FSK_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

MM_FSK_CPPFLAGS += -I$(MM_FSK_DIR)
MM_FSK_CFLAGS +=
MM_FSK_LDFLAGS += -lfftw3f -lm
MM_FSK_SRCS += \
$(MM_FSK_DIR)/fsk.c \
$(MM_FSK_DIR)/baudot.c
