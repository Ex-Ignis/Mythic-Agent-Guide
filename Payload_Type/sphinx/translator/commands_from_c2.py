import json
import base64
import logging

from .utils import (
    MYTHIC_CHECK_IN,
    MYTHIC_GET_TASKING,
    UUID_LEN,
    command_to_id,
    TASK_COMMANDS,
)
from .tlv_packer import BinPacker

log = logging.getLogger(__name__)


def checkin_to_agent_format(uuid: str) -> bytes:
    packer = BinPacker()
    packer.pack_byte(MYTHIC_CHECK_IN)
    packer.buffer.extend(uuid.encode("utf-8"))
    packer.pack_byte(0x01)
    return packer.get_bytes()


def get_responses_to_agent_format(inputMsg) -> bytes:
    tasks = inputMsg.Message.get("tasks", [])
    responses = inputMsg.Message.get("responses", [])
    delegates = inputMsg.Message.get("delegates", [])
    socks = inputMsg.Message.get("socks", [])

    all_tasks = list(tasks)

    for resp in responses:
        task_id = resp.get("task_id")
        file_id = resp.get("file_id")
        chunk_num = resp.get("chunk_num")
        total_chunks = resp.get("total_chunks")
        chunk_data = resp.get("chunk_data")

        log.info("get_responses_to_agent_format: resp task=%s file=%s chunk=%s data=%s total=%s",
                 task_id, file_id, chunk_num,
                 (len(chunk_data) if chunk_data else None), total_chunks)

        if file_id and chunk_data:
            params = {"chunk_num": chunk_num or 0, "chunk_data": chunk_data}
            if total_chunks is not None:
                params["total_chunks"] = total_chunks
            all_tasks.append({
                "command": "upload_resp",
                "id": task_id,
                "parameters": json.dumps(params),
            })
        elif file_id and not chunk_data:
            all_tasks.append({
                "command": "download_resp",
                "id": task_id,
                "parameters": json.dumps({"file_id": file_id}),
            })

    for socks_msg in socks:
        server_id = socks_msg.get("server_id", 0)
        data_b64 = socks_msg.get("data") or ""
        exit_flag = socks_msg.get("exit", False)
        all_tasks.append({
            "command": "socks_resp",
            "id": "00000000-0000-0000-0000-000000000000",
            "parameters": json.dumps({
                "server_id": server_id,
                "data": data_b64,
                "exit": exit_flag,
            }),
        })

    packer = BinPacker()
    packer.pack_byte(MYTHIC_GET_TASKING)
    packer.pack_uint32(len(all_tasks))

    for task in all_tasks:
        task_data = _pack_single_task(task)
        packer.buffer.extend(task_data)

    return packer.get_bytes()


def _pack_single_task(task: dict) -> bytes:
    command_name = task["command"]
    task_uuid = task["id"]
    params_str = task.get("parameters", "")

    # Record which command owns this task_id so the response parser in
    # commands_from_implant can format output correctly (ps -> processes, ls -> file_browser).
    TASK_COMMANDS[task_uuid] = command_name

    cmd_id = command_to_id(command_name)

    if params_str:
        try:
            params = json.loads(params_str)
            if not isinstance(params, dict):
                # No structured params: Mythic sends the operator's raw string
                # (e.g. "3" for sleep) — wrap it as "command"
                params = {"command": params_str}
        except json.JSONDecodeError:
            params = {"command": params_str}
    else:
        params = {}

    inner = BinPacker()
    inner.pack_byte(cmd_id)
    inner.buffer.extend(task_uuid.encode("utf-8"))
    inner.pack_uint32(len(params))

    for pname, pvalue in params.items():
        _pack_param(inner, pname, pvalue)

    body = inner.get_bytes()

    outer = BinPacker()
    outer.pack_uint32(len(body))
    outer.buffer.extend(body)

    return outer.get_bytes()


def _pack_param(packer: BinPacker, name: str, value) -> None:
    inner = BinPacker()

    if isinstance(value, bool):
        inner.pack_byte(0x01 if value else 0x00)
    elif isinstance(value, int):
        inner.pack_uint32(value)
    elif name in ("chunk_data", "data", "module_file") and isinstance(value, str):
        decoded = base64.b64decode(value)
        inner.buffer.extend(decoded)
    elif isinstance(value, str):
        inner.buffer.extend(value.encode("utf-8"))
    elif isinstance(value, bytes):
        inner.buffer.extend(value)
    else:
        inner.buffer.extend(str(value).encode("utf-8"))

    data = inner.get_bytes()
    packer.pack_uint32(len(data))
    packer.buffer.extend(data)
