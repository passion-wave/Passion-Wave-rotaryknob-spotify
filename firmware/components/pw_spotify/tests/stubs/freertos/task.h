#pragma once
#include "FreeRTOS.h"
int xTaskCreate(void (*)(void *), const char *, uint32_t, void *, unsigned, TaskHandle_t *);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(int, uint32_t);
void vTaskDelete(TaskHandle_t);
