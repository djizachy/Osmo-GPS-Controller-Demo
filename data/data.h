#ifndef __DATA_H__
#define __DATA_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

void data_init(void);

bool is_data_layer_initialized(void);

esp_err_t data_write_with_response(uint16_t seq, const uint8_t *data, size_t length);

esp_err_t data_write_without_response(uint16_t seq, const uint8_t *data, size_t length);

esp_err_t data_wait_for_result_by_seq(uint16_t seq, int timeout_ms, cJSON **out_json);

esp_err_t data_wait_for_result_by_cmd(uint8_t cmd_set, uint8_t cmd_id, int timeout_ms, cJSON **out_json, uint16_t *out_seq);

typedef void (*camera_status_update_cb_t)(cJSON *parsed_data);
void data_register_status_update_callback(camera_status_update_cb_t callback);
void receive_camera_notify_handler(const uint8_t *data, size_t length);

#endif
