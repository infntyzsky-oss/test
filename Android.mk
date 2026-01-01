LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE            := reshade
LOCAL_SRC_FILES         := reshade.cpp
LOCAL_CPP_EXTENSION     := .cpp

# Optimasi + size kecil
LOCAL_CFLAGS            := -O3 -fvisibility=hidden -ffunction-sections -fdata-sections
LOCAL_LDFLAGS           := -Wl,--gc-sections

# Library wajib buat hook + GL
LOCAL_LDLIBS            := -llog -ldl -lEGL -lGLESv2

# Target lama biar kompatibel GTASA
LOCAL_ARM_MODE          := arm

include $(BUILD_SHARED_LIBRARY)
