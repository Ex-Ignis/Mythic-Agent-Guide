# Message types — must match Include/protocol.h exactly.
# Family 'A' = agent control, family 'D' = data / file transfers.
MYTHIC_CHECK_IN = 0xA1
MYTHIC_GET_TASKING = 0xA2
MYTHIC_TASK_RESPONSE = 0xA4
MYTHIC_INIT_DOWNLOAD = 0xD1
MYTHIC_CONT_DOWNLOAD = 0xD2
MYTHIC_UPLOAD_CHUNKED = 0xD3
MYTHIC_SOCKS_DATA = 0xD8
MYTHIC_FILE_BROWSER = 0xD9
MYTHIC_SHELL_DATA = 0xDA

STATUS_COMPLETE = 0x95
STATUS_FAILED = 0x99

UUID_LEN = 36

COMMANDS = {
    "shell": 0x60,
    "shell_input": 0x61,
    "shell_exit": 0x62,
    "shell_resize": 0x63,
    "sleep": 0x38,
    "exit": 0x80,
    "pwd": 0x43,
    "ls": 0x41,
    "cd": 0x42,
    "ps": 0x52,
    "info": 0x70,
    "net": 0x71,
    "sec": 0x72,
    "download": 0x51,
    "upload": 0x50,
    "download_resp": 0x53, # synthetic: carries the file_id Mythic returns after INIT
    "upload_resp": 0x54, # synthetic: carries chunk_data/chunk_num/total_chunks from the pull response
    "load": 0x90,
    "unload": 0x91,
    "list": 0x92,
}

def command_to_id(name: str) -> int:
    return COMMANDS.get(name, 0x00)


# task_id -> command name mapping. Populated when tasking is sent (commands_from_c2);
# read by the response parser (commands_from_implant) to know how to format output
# (e.g. ps -> processes, ls -> file_browser).
TASK_COMMANDS = {}
