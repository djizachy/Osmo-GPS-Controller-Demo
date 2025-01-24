# Add Camera Sleep Feature Example Documentation

The objective of this document is to implement a feature that allows the camera to enter sleep mode with a single press of the BOOT button, based on this open-source project.

First, we can obtain the corresponding command frame and response frame structure information from the document, as shown below:

```
Camera Power Mode Setting

(CmdSet = 0x00, CmdID = 0x1A), the data segment details are as follows:

| Frame Type      | Offset | Size | Name       | Type    | Description                                       |
|-----------------|--------|------|------------|---------|---------------------------------------------------|
| Command Frame   | 0      | 1    | power mode | uint8_t | Power mode setting: 0: Normal mode, 3: Sleep mode |
| Response Frame  | 0      | 1    | ret code   | uint8_t | Refer to the common return code                   |
```

## Protocol Layer Modification

First, we need to define the command frame and response frame in `dji_protocol_data_structures.h`:

```c
typedef struct __attribute__((packed)) {
    uint8_t power_mode;            // Power mode, 0 for normal mode, 3 for sleep mode
} camera_power_mode_switch_command_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t ret_code;              // Return code: 0 for success, non-zero for failure
} camera_power_mode_switch_response_frame_t;
```

Then, define the corresponding `creator` and `parser` functions in `dji_protocol_data_descriptors.c`:

```c
uint8_t* camera_power_mode_switch_creator(const void *structure, size_t *data_length, uint8_t cmd_type) {
    if (structure == NULL || data_length == NULL) {
        ESP_LOGE(TAG, "Invalid input: structure or data_length is NULL");
        return NULL;
    }

    uint8_t *data = NULL;

    // Check if it's a command frame
    if ((cmd_type & 0x20) == 0) {
        const camera_power_mode_switch_command_frame_t *command_frame = 
            (const camera_power_mode_switch_command_frame_t *)structure;

        *data_length = sizeof(camera_power_mode_switch_command_frame_t);

        ESP_LOGI(TAG, "Data length calculated for camera_power_mode_switch_command_frame: %zu", *data_length);

        data = (uint8_t *)malloc(*data_length);
        if (data == NULL) {
            ESP_LOGE(TAG, "Memory allocation failed in camera_power_mode_switch_creator");
            return NULL;
        }

        ESP_LOGI(TAG, "Memory allocation succeeded for command frame, copying data...");

        memcpy(data, command_frame, *data_length);
    } else {
        // Response frame creation for this functionality is not yet supported.
        ESP_LOGE(TAG, "Response frames are not supported in camera_power_mode_switch_creator");
        return NULL;
    }

    return data;
}

int camera_power_mode_switch_parser(const uint8_t *data, size_t data_length, void *structure_out, uint8_t cmd_type) {
    if (data == NULL || structure_out == NULL) {
        ESP_LOGE(TAG, "camera_power_mode_switch_parser: NULL input detected");
        return -1;
    }

    ESP_LOGI(TAG, "Parsing Camera Power Mode Switch data, received data length: %zu", data_length);

    if ((cmd_type & 0x20) == 0) {
        // Command frame parsing for this functionality is not yet supported.
        ESP_LOGE(TAG, "camera_power_mode_switch_parser: Only response frames are supported");
        return -1;
    }

    if (data_length < sizeof(camera_power_mode_switch_response_frame_t)) {
        ESP_LOGE(TAG, "camera_power_mode_switch_parser: Data length too short for response frame. Expected: %zu, Got: %zu",
                 sizeof(camera_power_mode_switch_response_frame_t), data_length);
        return -1;
    }

    const camera_power_mode_switch_response_frame_t *response = (const camera_power_mode_switch_response_frame_t *)data;
    camera_power_mode_switch_response_frame_t *output_response = (camera_power_mode_switch_response_frame_t *)structure_out;

    output_response->ret_code = response->ret_code;

    ESP_LOGI(TAG, "Camera Power Mode Switch Response parsed successfully. ret_code: %u", output_response->ret_code);

    return 0;
}
```

Next, declare the `creator` and `parser` functions in `dji_protocol_data_descriptors.h`:

```c
uint8_t* camera_power_mode_switch_creator(const void *structure, size_t *data_length, uint8_t cmd_type);
int camera_power_mode_switch_parser(const uint8_t *data, size_t data_length, void *structure_out, uint8_t cmd_type);
```

Finally, add the corresponding triples to the array in `dji_protocol_data_descriptors.c`:

```c
const data_descriptor_t data_descriptors[] = {
    // Camera power mode switch
    {0x00, 0x1A, (data_creator_func_t)camera_power_mode_switch_creator, (data_parser_func_t)camera_power_mode_switch_parser},
};
```

## Logic Layer Function Creation

Create a new function in `command_logic.c`:

```c
camera_power_mode_switch_response_frame_t* command_logic_power_mode_switch_sleep(void) {
    // Log message indicating power mode switch to sleep
    ESP_LOGI(TAG, "%s: Reporting power mode switch to sleep", __FUNCTION__);

    // Check if the protocol is connected
    if (connect_logic_get_state() != PROTOCOL_CONNECTED) {
        ESP_LOGE(TAG, "Protocol connection to the camera failed. Current connection state: %d", connect_logic_get_state());
        return NULL;
    }

    uint16_t seq = generate_seq();  // Generate sequence number

    // Create command frame with power mode set to sleep mode (0x03)
    camera_power_mode_switch_command_frame_t command_frame = {
        .power_mode = 0x03,        // Set to 0x03 for sleep mode
    };

    // Send the command and receive the response
    CommandResult result = send_command(
        0x00,
        0x1A,                 // Use CmdSet = 0x00 and CmdID = 0x1A for power mode switch
        CMD_RESPONSE_OR_NOT,
        &command_frame,
        seq,
        5000
    );

    // If no response structure is returned, the send or receive failed
    if (result.structure == NULL) {
        ESP_LOGE(TAG, "Failed to send command or receive response");
        return NULL;
    }

    // Convert the returned data structure to the corresponding response frame
    camera_power_mode_switch_response_frame_t *response = (camera_power_mode_switch_response_frame_t *)result.structure;

    // Log the response information for power mode switch
    ESP_LOGI(TAG, "Power Mode Switch Response: ret_code=%d", response->ret_code);

    return response;  // Return the parsed response frame
}
```

In `command_logic.h` declare:

```c
camera_power_mode_switch_response_frame_t* command_logic_power_mode_switch_sleep(void);
```

## Modify Key Logic

In `key_logic.c`, modify the `handle_boot_single_press` function to:

```c
static void handle_boot_single_press() {
    // Switch to sleep mode
    camera_power_mode_switch_response_frame_t *response = command_logic_power_mode_switch_sleep();

    if (response != NULL) {
        // Log the response code
        ESP_LOGI(TAG, "Power mode switch to sleep completed. ret_code=%d", response->ret_code);

        // Be sure to free the memory, or it may lead to memory exhaustion
        free(response);
    } else {
        // If the switch fails, log an error
        ESP_LOGE(TAG, "Failed to switch power mode to sleep.");
    }
}
```

## Test

After completing the code modifications, follow these steps to test:

1. Rebuild the project.

2. Flash the firmware to the development board via USB cable.

3. Long press the BOOT button until a Bluetooth connection is established with the camera (the LED indicator will show the connection status).

4. Once connected, press the BOOT button again, and you should observe the camera entering sleep mode with the display turning off.

If everything works correctly, the camera will successfully enter sleep mode. You can check the serial log to confirm the execution status of the command and the return code.
