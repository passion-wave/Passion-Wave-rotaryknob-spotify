#pragma once
#include <stdint.h>
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
