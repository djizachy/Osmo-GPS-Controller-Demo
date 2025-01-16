#include <string.h>
#include <stdio.h>
#include "esp_log.h"

#include "dji_protocol_data_processor.h"

#define TAG "DJI_PROTOCOL_DATA_PROCESSOR"

const data_descriptor_t *find_data_descriptor(uint8_t cmd_set, uint8_t cmd_id) {
    for (size_t i = 0; i < DATA_DESCRIPTORS_COUNT; ++i) {
        if (data_descriptors[i].cmd_set == cmd_set && data_descriptors[i].cmd_id == cmd_id) {
            return &data_descriptors[i];
        }
    }
    return NULL;
}

int data_parser_by_structure(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const uint8_t *data, size_t data_length, void *structure_out) {
    fprintf(stderr, "Parsing CmdSet: 0x%02X, CmdID: 0x%02X, CmdType: 0x%02X\n", cmd_set, cmd_id, cmd_type);

    const data_descriptor_t *descriptor = find_data_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        fprintf(stderr, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return -1;
    }

    if (descriptor->parser == NULL) {
        fprintf(stderr, "Parser function is NULL for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return -1;
    }

    return descriptor->parser(data, data_length, structure_out, cmd_type);
}

uint8_t* data_creator_by_structure(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type, const void *structure, size_t *data_length) {
    const data_descriptor_t *descriptor = find_data_descriptor(cmd_set, cmd_id);
    if (descriptor == NULL) {
        fprintf(stderr, "Descriptor not found for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return NULL;
    }

    if (descriptor->creator == NULL) {
        fprintf(stderr, "Creator function is NULL for CmdSet: 0x%02X, CmdID: 0x%02X\n", cmd_set, cmd_id);
        return NULL;
    }

    return descriptor->creator(structure, data_length, cmd_type);
}
