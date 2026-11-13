// Logging nativo a logcat con el tag "turok2" (adb logcat -s turok2:*).
#pragma once

#include <android/log.h>

#define LOG_TAG "turok2"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
