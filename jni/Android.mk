LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := ao_inject
LOCAL_SRC_FILES := ao_inject.cpp
LOCAL_LDLIBS := -llog -ldl -landroid
LOCAL_CFLAGS := -O3 -fvisibility=hidden -Wall -Wextra
LOCAL_CPPFLAGS := -std=c++14
LOCAL_ARM_MODE := thumb

include $(BUILD_SHARED_LIBRARY)
