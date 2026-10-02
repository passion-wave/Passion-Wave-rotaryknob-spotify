#pragma once
void pw_test_logi(const char *tag, const char *format, ...);
#define ESP_LOGI(...) pw_test_logi(__VA_ARGS__)
