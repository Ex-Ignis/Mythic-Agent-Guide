import base64
import ipaddress
import logging
import re

_ANSI_ESC = re.compile(
    r'\x1b'
    r'(?:'
    r'\[[0-?]*[ -/]*[@-~]'
    r'|\][^\x07\x1b]*(?:\x07|\x1b\\)'
    r'|[@-Z\\-_]'
    r')'
)

from .utils import (
    MYTHIC_CHECK_IN,
    MYTHIC_GET_TASKING,
    MYTHIC_TASK_RESPONSE,
    MYTHIC_INIT_DOWNLOAD,
    MYTHIC_CONT_DOWNLOAD,
    MYTHIC_UPLOAD_CHUNKED,
    MYTHIC_SHELL_DATA,
    STATUS_COMPLETE,
    STATUS_FAILED,
    TASK_COMMANDS,
)

log = logging.getLogger(__name__)


def _get_bytes_with_size(data: bytes) -> tuple:
    size = int.from_bytes(data[0:4], "big")
    data = data[4:]
    return data[:size], data[size:]


def checkin_to_mythic_format(data: bytes) -> dict:
    callback_uuid = data[:36]
    data = data[36:]

    num_ips = int.from_bytes(data[0:4], "big")
    data = data[4:]
    ips = []
    for _ in range(num_ips):
        ip_bytes = data[:4]
        data = data[4:]
        ips.append(str(ipaddress.IPv4Address(ip_bytes)))

    target_os, data = _get_bytes_with_size(data)

    arch_byte = data[0]
    data = data[1:]
    arch = "x64" if arch_byte == 0x64 else "x86" if arch_byte == 0x86 else ""

    hostname, data = _get_bytes_with_size(data)
    username, data = _get_bytes_with_size(data)
    domain, data = _get_bytes_with_size(data)

    pid = int.from_bytes(data[0:4], "big")
    data = data[4:]

    process_name, data = _get_bytes_with_size(data)
    external_ip, data = _get_bytes_with_size(data)

    return {
        "action": "checkin",
        "ips": ips,
        "os": target_os.decode("cp850", errors="replace"),
        "user": username.decode("cp850", errors="replace"),
        "host": hostname.decode("cp850", errors="replace"),
        "domain": domain.decode("cp850", errors="replace"),
        "pid": pid,
        "uuid": callback_uuid.decode("utf-8"),
        "architecture": arch,
        "process_name": process_name.decode("cp850", errors="replace"),
        "external_ip": external_ip.decode("cp850", errors="replace"),
    }


def post_response_handler(data: bytes) -> dict:
    messages = []

    num_tasks = int.from_bytes(data[0:4], "big")
    data = data[4:]

    while len(data) > 0:
        if len(data) < 1:
            break

        response_type = data[0]
        data = data[1:]

        if response_type == MYTHIC_TASK_RESPONSE:
            result = _parse_task_response(data)
            if result is None:
                break
            task_json, data = result

        elif response_type == MYTHIC_INIT_DOWNLOAD:
            task_json, data = _parse_download_init(data)

        elif response_type == MYTHIC_CONT_DOWNLOAD:
            task_json, data = _parse_download_cont(data)

        elif response_type == MYTHIC_UPLOAD_CHUNKED:
            task_json, data = _parse_upload(data)

        elif response_type == MYTHIC_SHELL_DATA:
            task_json, data = _parse_shell_data(data)

        else:
            log.info(f"Unknown response type: {hex(response_type)}")
            break

        if task_json is not None:
            messages.append(task_json)

    return {
        "action": "get_tasking",
        "tasking_size": num_tasks,
        "responses": messages,
    }


def _parse_ps_output(output_str: str):
    """Convert the ps table (PID\\tPPID\\tThreads\\tName) into the process list
    format expected by Mythic's Process Browser."""
    procs = []
    lines = output_str.split("\n")
    for line in lines[1:]:  # skip header row
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        try:
            procs.append({
                "process_id": int(parts[0]),
                "parent_process_id": int(parts[1]),
                "threads": int(parts[2]),
                "name": parts[3],
            })
        except ValueError:
            continue
    return procs


def _parse_ls_output(output_str: str):
    """Convert ls output into Mythic's file_browser blob.
    Agent output format:
        <directory>:
        <name>\\t<size>\\tDIR|FILE
    """
    lines = output_str.split("\n")
    if not lines or not lines[0].rstrip().endswith(":"):
        # No directory header (e.g. "ls: could not open...") — not a file listing
        return None

    # First line: "<directory>:" — strip the trailing ':' and optional trailing '\'
    dir_path = lines[0].rstrip(":").rstrip("\\")
    if len(dir_path) == 2 and dir_path[1] == ":":       # raiz de unidad "C:"
        parent_path, name = "", dir_path
    elif "\\" in dir_path:
        idx = dir_path.rfind("\\")
        parent_path, name = dir_path[:idx], dir_path[idx + 1:]
    else:
        parent_path, name = "", dir_path

    files = []
    for line in lines[1:]:
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        try:
            size = int(parts[1])
        except ValueError:
            size = 0
        files.append({
            "name": parts[0],
            "size": size,
            "is_file": parts[2].strip() == "FILE",
        })

    return {
        "host": "",          # mismo host que el callback -> Mythic lo rellena
        "is_file": False,
        "name": name,
        "parent_path": parent_path,
        "success": True,
        "files": files,
    }


def _parse_task_response(data: bytes):
    if len(data) < 36:
        return None

    task_uuid = data[:36].decode("utf-8")
    data = data[36:]

    output, data = _get_bytes_with_size(data)
    output_str = output.decode("cp850", errors="replace") if output else ""

    if len(data) < 1:
        return None

    status_byte = data[0]
    data = data[1:]

    if status_byte == STATUS_COMPLETE:
        status = "success"
    elif status_byte == STATUS_FAILED:
        status = "error"
    else:
        status = None

    user_output = f"[+] Agent response:\n{output_str}" if output_str else "[+] Agent checked in, no output"

    task_json = {
        "task_id": task_uuid,
        "user_output": user_output,
        "status": status,
        "completed": status in ("success", "error"),
    }

    # Structured output for the UI, keyed on the command that owns this task
    cmd = TASK_COMMANDS.get(task_uuid)
    if output_str:
        if cmd == "ps":
            procs = _parse_ps_output(output_str)
            if procs:
                task_json["processes"] = procs
        elif cmd == "ls":
            fb = _parse_ls_output(output_str)
            if fb is not None:
                task_json["file_browser"] = fb

    return task_json, data


def _parse_download_init(data: bytes):
    if len(data) < 36:
        return None

    task_uuid = data[:36].decode("utf-8")
    data = data[36:]

    if len(data) < 4:
        return None
    total_chunks = int.from_bytes(data[0:4], "big")
    data = data[4:]

    full_path, data = _get_bytes_with_size(data)

    if len(data) < 4:
        return None
    chunk_size = int.from_bytes(data[0:4], "big")
    data = data[4:]

    return {
        "task_id": task_uuid,
        "download": {
            "total_chunks": total_chunks,
            "full_path": full_path.decode("cp850", errors="replace"),
            "chunk_size": chunk_size,
            "is_screenshot": False,
        },
    }, data


def _parse_download_cont(data: bytes):
    if len(data) < 36:
        return None

    task_uuid = data[:36].decode("utf-8")
    data = data[36:]

    chunk_num = int.from_bytes(data[0:4], "big")
    data = data[4:]

    if len(data) < 36:
        return None
    file_id = data[:36].decode("utf-8")
    data = data[36:]

    chunk_bytes, data = _get_bytes_with_size(data)
    chunk_b64 = base64.b64encode(chunk_bytes).decode("utf-8")

    chunk_size = int.from_bytes(data[0:4], "big")
    data = data[4:]

    return {
        "task_id": task_uuid,
        "download": {
            "chunk_num": chunk_num,
            "file_id": file_id,
            "chunk_data": chunk_b64,
            "chunk_size": chunk_size,
        },
    }, data


def _parse_upload(data: bytes):
    log.info("_parse_upload: len=%d", len(data))
    if len(data) < 36:
        return None

    task_uuid = data[:36].decode("utf-8")
    data = data[36:]

    if len(data) < 4:
        return None
    chunk_num = int.from_bytes(data[0:4], "big")
    data = data[4:]

    if len(data) < 36:
        return None
    file_id = data[:36].decode("utf-8")
    data = data[36:]

    full_path, data = _get_bytes_with_size(data)

    if len(data) < 4:
        return None
    chunk_size = int.from_bytes(data[0:4], "big")
    data = data[4:]

    log.info("_parse_upload: task=%s chunk=%d file=%s path=%s csize=%d",
             task_uuid, chunk_num, file_id, full_path, chunk_size)

    return {
        "task_id": task_uuid,
        "upload": {
            "chunk_num": chunk_num,
            "file_id": file_id,
            "full_path": full_path.decode("cp850", errors="replace"),
            "chunk_size": chunk_size,
        },
    }, data


def _parse_shell_data(data: bytes):
    # Wire format: [task_uuid(36)][message_type(1)][size(4)][content]
    if len(data) < 38:  # 36 uuid + 1 type + min 1 byte size
        return None, data

    task_uuid = data[:36].decode("utf-8")
    data = data[36:]

    # message_type: 1 = stdout/stderr output (reserved for future sub-types)
    _message_type = data[0]
    data = data[1:]

    content, data = _get_bytes_with_size(data)
    content_str = content.decode("cp850", errors="replace") if content else ""
    content_str = _ANSI_ESC.sub("", content_str).replace("\r\n", "\n").replace("\r", "\n")

    log.info("_parse_shell_data: task=%s type=%d len=%d", task_uuid, _message_type, len(content_str))

    return {
        "task_id": task_uuid,
        "user_output": content_str,
        "completed": False,  # shell task stays open until shell_exit closes it
    }, data
