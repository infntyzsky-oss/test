LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := reshade
LOCAL_SRC_FILES := reshade.cpp

LOCAL_LDLIBS := -llog -lEGL -lGLESv3 -ldl

LOCAL_CPPFLAGS := -std=c++17 -fvisibility=hidden

include $(BUILD_SHARED_LIBRARY)
