LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := reshade
LOCAL_SRC_FILES := reshade.cpp
LOCAL_CPP_EXTENSION := .cpp

LOCAL_CFLAGS += -O3 -fvisibility=hidden -w
LOCAL_LDLIBS := -llog -lEGL -lGLESv3 -ldl

include $(BUILD_SHARED_LIBRARY)
