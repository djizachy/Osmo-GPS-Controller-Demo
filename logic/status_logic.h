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

#ifndef STATUS_LOGIC_H
#define STATUS_LOGIC_H

#include <stdint.h>

// 相机状态全局变量声明（其他的后续可补充）
// Declaration of global variables for camera status (more can be added later)
extern uint8_t current_camera_mode;
extern uint8_t current_camera_status;
extern uint8_t current_video_resolution;
extern uint8_t current_fps_idx;
extern uint8_t current_eis_mode;
extern bool camera_status_initialized;

bool is_camera_recording();

void print_camera_status();

int subscript_camera_status(uint8_t push_mode, uint8_t push_freq);

void update_camera_state_handler(void *data);

#endif