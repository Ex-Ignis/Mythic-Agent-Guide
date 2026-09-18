# sphinx-agent

Educational Windows x64 Mythic C2 agent written in C, ConPTY interactive shell, COFF module loader, AES-256-CBC, TLV, HTTP transport.

> **Purpose:** learning project. Covers the full stack of a Mythic payload type: C agent, Python Mythic integration, and a binary translator. Not intended for production offensive use, may contain errors, instability, or inaccuracies.


# Architecture

```sh
Operator (Mythic UI)
        |
        | JSON (Mythic internal)
        v
  SphinxTranslator  ←──────────────────────────────────────────────┐
  (Python container)                                               |
        |  TLV binary over HTTP                                    |
        v                                                          |
   httpx listener                                                 |
        |                                                          |
        | AES-256-CBC encrypted TLV blob                           |
        v                                                          |
   Sphinx agent (Windows x64 .exe)  ──── response TLV ────────────>┘
```

The agent never speaks JSON. All traffic is a custom TLV binary protocol, AES-256-CBC encrypted, base64-encoded at the HTTP layer. The translator container converts between the agent's binary wire format and the JSON Mythic expects internally.


# Protocol

## Wire format

```
[type: 1 byte][uuid: 36 bytes][size: 4 bytes BE][data: size bytes]
```

Each HTTP POST body may carry multiple TLV blocks concatenated. The outer type byte determines how the translator parses the payload.

## Message types (protocol.h / utils.py)

| Constant           | Value | Direction     | Description                           |
|--------------------|-------|---------------|---------------------------------------|
| MSG_CHECKIN        | 0xA1  | agent → C2    | Initial check-in with host metadata   |
| MSG_GET_TASKING    | 0xA2  | agent → C2    | Task request (outer GET/POST byte)    |
| MSG_TASK_RESPONSE  | 0xA4  | agent → C2    | Task result (status + output)         |
| MSG_DOWNLOAD_INIT  | 0xD1  | agent → C2    | Download start (registers the file)   |
| MSG_DOWNLOAD_CONT  | 0xD2  | agent → C2    | Download chunk                        |
| MSG_UPLOAD_PULL    | 0xD3  | agent → C2    | Request next upload chunk             |
| MSG_SHELL_DATA     | 0xDA  | agent → C2    | Interactive shell stdout/stderr       |

## Beacon loop

```
GET  /index  (agent_uuid in body, AES encrypted)
             → Mythic returns tasking as TLV

[for each task: execute, build response TLV]

POST /data   (results TLV, AES encrypted)
             → Mythic may return more tasks (synthetic file acks)

[repeat POST until no more tasks, then sleep+jitter]
```

# Components

## Agent (C, `agent_code/`)

| File           | Responsibility                                                    |
|----------------|-------------------------------------------------------------------|
| `main.c`       | Beacon loop: GET → dispatch → POST → sleep                       |
| `comms.c`      | HTTP GET/POST via WinHTTP; AES-256-CBC encrypt/decrypt            |
| `packer.c`     | Builds check-in TLV (hostname, IPs, OS, PID, arch, username)     |
| `parser.c`     | Parses incoming TLV task list into `Task[]`                       |
| `dispatcher.c` | Routes tasks to built-in handlers or loaded COFF modules          |
| `shell.c`      | ConPTY interactive shell with anonymous-pipe fallback             |
| `loader.c`     | COFF (.o) in-memory loader                                        |
| `module.c`     | Runtime module table (load / unload / dispatch)                   |
| `outbound.c`   | Accumulates outbound TLV blocks before the POST                   |
| `file_transfer.c` | Chunked download and upload state machines                     |
| `api_table.c`  | Function pointer table (`ApiTable`) passed to COFF modules        |
| `crypto.c`     | AES-256-CBC + HMAC-SHA256 via aes.c / sha256.c / hmac_sha256.c   |

## Built-in commands (dispatcher.c)

| ID   | Name         | Description                              |
|------|--------------|------------------------------------------|
| 0x38 | sleep        | Set beacon interval and jitter           |
| 0x50 | upload       | Write a file from Mythic to disk         |
| 0x51 | download     | Send a file from disk to Mythic          |
| 0x60 | shell        | Spawn an interactive shell (ConPTY)      |
| 0x61 | shell_input  | Send stdin bytes to the running shell    |
| 0x62 | shell_exit   | Tear down the shell session              |
| 0x63 | shell_resize | Resize the ConPTY window                 |
| 0x80 | exit         | Terminate the agent                      |
| 0x90 | load         | Load a COFF module                       |
| 0x91 | unload       | Unload a COFF module by command ID       |
| 0x92 | list         | List loaded modules                      |

## COFF module loader

`loader.c` loads a standard x64 COFF `.o` file entirely in memory:

1. Single `VirtualAlloc` for all sections (RWX).
2. Applies internal relocations (`IMAGE_REL_AMD64_REL32`, `ADDR32NB`, `ADDR64`).
3. Resolves the `go` entry point by symbol name.
4. Calls `go(ApiTable*, uuid, Param*, param_count)`.

The `ApiTable` struct gives modules access to Win32 APIs without importing them directly — the loader resolves everything at startup via `GetProcAddress`.

Modules shipped in `modules/`: `cd`, `ls`, `pwd`, `ps`, `info`, `net`, `sec`.

## Interactive shell (shell.c)

Two code paths selected at runtime:

- **ConPTY** (Win10 1809+): uses `CreatePseudoConsole` / `CreateProcess`. The shell thinks it is attached to a real terminal and emits VT100/ANSI sequences; the translator strips them with a regex before forwarding to the operator.
- **Anonymous pipe fallback** (older Windows): `CreatePipe` + `STARTF_USESTDHANDLES` + `-NonInteractive` flag.

`shell_stop` calls `CancelSynchronousIo(hReader)` before `WaitForSingleObject` to guarantee the reader thread exits before `DeleteCriticalSection` and `free(rbuf)` run — this was the root cause of the original shell_exit crash.


## Mythic integration (Python, `mythic/agent_functions/`)

| File             | Description                                             |
|------------------|---------------------------------------------------------|
| `sphinx.py`      | Payload type definition (name, OS, arch, build args)   |
| `builder.py`     | Stamps `config.h`, runs `make` inside the container    |
| `commands/*.py`  | One file per Mythic command (UI parameters, help text) |

## Translator (Python, `translator/`)

| File                     | Description                                                  |
|--------------------------|--------------------------------------------------------------|
| `translator.py`          | `SphinxTranslator` — implements `TranslationContainer`       |
| `commands_from_c2.py`    | Mythic JSON → agent TLV binary                               |
| `commands_from_implant.py` | Agent TLV binary → Mythic JSON (ps/ls structured output) |
| `tlv_packer.py`          | `BinPacker` helper (pack_byte, pack_uint32, pack_bytes)      |
| `utils.py`               | Protocol constants and `TASK_COMMANDS` mapping               |

# Repository layout

```
sphinx-agent/
├── .gitignore
├── config.json                          # Mythic payload type config
└── Payload_Type/sphinx/
    ├── Dockerfile
    ├── main.py                          # container entrypoint
    └── sphinx/
        ├── agent_code/
        │   ├── Include/                 # .h headers (config.h has placeholder values)
        │   ├── Src/                     # .c agent sources
        │   ├── modules/                 # COFF module sources
        │   └── Makefile
        ├── mythic/
        │   └── agent_functions/
        │       ├── builder.py
        │       ├── sphinx.py
        │       └── commands/
        └── translator/
```

# Configuration

`Include/config.h` ships with placeholder values. The Mythic builder (`builder.py`) stamps the real values at payload-build time. **Never commit real IPs, ports, or AES keys.**

| Define          | Default              | Description                          |
|-----------------|----------------------|--------------------------------------|
| `CALLBACK_HOST` | `L"127.0.0.1"`       | Mythic server IP or hostname         |
| `CALLBACK_PORT` | `80`                 | Listener port                        |
| `CALLBACK_PATH` | `L"/index"`          | GET URI                              |
| `POST_PATH`     | `L"/data"`           | POST URI                             |
| `SLEEP_TIME`    | `10`                 | Beacon interval (seconds)            |
| `JITTER`        | `10`                 | Jitter percentage                    |
| `DEBUG`         | `1`                  | `1` = console window; `0` = hidden   |
| `PAYLOAD_UUID`  | all-zeros UUID       | Stamped by builder                   |
| `ENC_KEY`       | 32 zero bytes        | AES-256 key stamped by builder       |

## Requirements

- **Build:** mingw-w64 cross-compiler (`x86_64-w64-mingw32-gcc`) — runs inside the Dockerfile
- **Runtime:** Windows x64, Win10 1809+ for ConPTY; older Windows uses anonymous-pipe fallback
- **C2:** [Mythic](https://github.com/its-a-feature/Mythic) 3.x with `httpx` profile (malleable config included)

See [BUILDING.md](BUILDING.md) for full build and deployment instructions.
