# Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| [Mythic](https://github.com/its-a-feature/Mythic) | 3.x | C2 framework |
| Docker + Docker Compose | any recent | Mythic runs in containers |
| Python | 3.8+ | Only needed for manual translator testing |

The agent itself compiles inside the Docker container — you do not need mingw-w64 on the host.

---

# 1. Add Sphinx to Mythic

```bash
# From your Mythic install directory
./mythic-cli payload install github https://github.com/Ex-Ignis/Mythic-Agent-Guide

# Or install from a local path (no internet required)
./mythic-cli payload install folder /path/to/sphinx-agent
```

Mythic copies `Payload_Type/sphinx/` into its payload directory and registers the type.

---

# 2. Start the services

```bash
./mythic-cli start sphinx
```

This starts two containers:
- `sphinx` — Python container running `main.py` (Mythic integration + builder)
- `sphinx_translator` — Python container running the `SphinxTranslator`

Verify they are healthy:

```bash
./mythic-cli status
```

Both should appear as `running`.

---

# 3. Configure a listener

In the Mythic UI, create an `httpx` profile and upload `httpx_config.json` as the malleable config:

| Field        | Value                    |
|--------------|--------------------------|
| Callback host| `<Mythic server IP>`     |
| Callback port| any free port (e.g. 80)  |
| GET URI      | `/index`                 |
| POST URI     | `/data`                  |

The `httpx_config.json` in the repo root is a ready-made malleable profile named `sphinx-mvp` that matches those URIs.

---

# 4. Build a payload

In the Mythic UI → Payloads → Generate:

1. Select payload type **sphinx**
2. Select the `raw_c2` profile you created
3. Set callback host/port (must match the listener)
4. Set sleep interval and jitter
5. Click Build

The builder (`builder.py`) inside the container:
1. Copies `agent_code/` to a temp directory
2. Stamps `config.h` with the real IP, port, UUID, and AES key
3. Runs `make` with `x86_64-w64-mingw32-gcc`
4. Returns the resulting `sphinx.exe`

---

# Manual build (without Mythic)

For development and debugging only — uses placeholder config values from `config.h`.

**Prerequisite:** install mingw-w64.

```bash
# Ubuntu / Debian
sudo apt install mingw-w64

# Windows (MSYS2)
pacman -S mingw-w64-x86_64-gcc
```

```bash
cd Payload_Type/sphinx/sphinx/agent_code

# Debug build (console window visible, debug symbols)
make DEBUG=1

# Release build (hidden window, stripped)
make
```

Output: `sphinx.exe` in `agent_code/`.

## Building COFF modules

```bash
cd Payload_Type/sphinx/sphinx/agent_code/modules
make
```

Each module compiles to a `<name>.o` file. Load them at runtime with the `load` command.

---

# 5. Run the agent

Copy `sphinx.exe` to a Windows x64 machine and run it. It will beacon to the configured C2 immediately.

For testing without Mythic, build with `DEBUG=1` and watch stderr for the `[SPHINX]` log lines.

---

# Manual translator testing

```bash
cd Payload_Type/sphinx
pip install mythic-container

# Start the translator locally (useful for packet-level debugging)
python -m sphinx.translator.translator
```

---

# config.h reference

The builder stamps these automatically. For a manual build, edit them by hand:

```c
#define CALLBACK_HOST  L"<your Mythic IP>"
#define CALLBACK_PORT  <port>
#define CALLBACK_PATH  L"/index"
#define POST_PATH      L"/data"
#define SLEEP_TIME     10          // seconds
#define JITTER         10          // percent
#define DEBUG          0           // 1 = console, 0 = hidden window
#define PAYLOAD_UUID   "<uuid-from-mythic>"
#define ENC_KEY        {0xAA, 0xBB, ...}   // 32-byte AES key from Mythic
```

**Never commit a `config.h` with real values.** The placeholder file in the repo compiles fine but will not connect to any real C2.
