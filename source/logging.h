#pragma once

#include <cstdio>

#ifdef __APPLE__
#include <os/log.h>

#define WRAPPER_LOG(fmt, ...) \
    os_log(OS_LOG_DEFAULT, "[VST3MCPWrapper] " fmt, ##__VA_ARGS__)
#define WRAPPER_LOG_ERROR(fmt, ...) \
    os_log_error(OS_LOG_DEFAULT, "[VST3MCPWrapper] " fmt, ##__VA_ARGS__)

#else

#define WRAPPER_LOG(fmt, ...) \
    fprintf(stderr, "[VST3MCPWrapper] " fmt "\n", ##__VA_ARGS__)
#define WRAPPER_LOG_ERROR(fmt, ...) \
    fprintf(stderr, "[VST3MCPWrapper] ERROR: " fmt "\n", ##__VA_ARGS__)

#endif
