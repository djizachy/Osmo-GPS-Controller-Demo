#ifndef __COMMAND_LOGIC_H__
#define __COMMAND_LOGIC_H__

#include "enums_logic.h"

#include "dji_protocol_data_structures.h"

uint16_t generate_seq(void);

typedef struct {
    void *structure;
    size_t length;  // This is not the length of structure, but the length of DATA segment excluding CmdSet and CmdID
                    // 这里的长度并不是 structure 长度，而是 DATA 段除去 CmdSet 和 CmdID 的长度
} CommandResult;

CommandResult send_command(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *structure, uint16_t seq, int timeout_ms);

camera_mode_switch_response_frame_t* command_logic_switch_camera_mode(camera_mode_t mode);

version_query_response_frame_t* command_logic_get_version(void);

record_control_response_frame_t* command_logic_start_record(void);

record_control_response_frame_t* command_logic_stop_record(void);

gps_data_push_response_frame* command_logic_push_gps_data(const gps_data_push_command_frame *gps_data);

key_report_response_frame_t* command_logic_key_report_qs(void);

#endif
