/*
 * Copyright (c) 2025 DJI
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __DATA_H__
#define __DATA_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

void data_init(void);

bool is_data_layer_initialized(void);

esp_err_t data_write_with_response(uint16_t seq, const uint8_t *raw_data, size_t raw_data_length);

esp_err_t data_write_without_response(uint16_t seq, const uint8_t *raw_data, size_t raw_data_length);

esp_err_t data_wait_for_result_by_seq(uint16_t seq, int timeout_ms, void **out_result, size_t *out_result_length);

esp_err_t data_wait_for_result_by_cmd(uint8_t cmd_set, uint8_t cmd_id, int timeout_ms, uint16_t *out_seq, void **out_result, size_t *out_result_length);

typedef void (*camera_status_update_cb_t)(void *data);
void data_register_status_update_callback(camera_status_update_cb_t callback);
void receive_camera_notify_handler(const uint8_t *raw_data, size_t raw_data_length);

#endif
