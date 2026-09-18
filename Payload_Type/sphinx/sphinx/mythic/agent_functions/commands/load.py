import base64
import json

from mythic_container.MythicCommandBase import *
from mythic_container.MythicRPC import *


class LoadArguments(TaskArguments):
    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = [
            CommandParameter(
                name="module_name",
                type=ParameterType.String,
                description="Module name (e.g. ls)",
                parameter_group_info=[ParameterGroupInfo(ui_position=1)],
            ),
            CommandParameter(
                name="module_file",
                type=ParameterType.File,
                description="Compiled COFF file (.o)",
                parameter_group_info=[ParameterGroupInfo(ui_position=2)],
            ),
        ]

    async def parse_arguments(self):
        if len(self.command_line) > 0:
            if self.command_line[0] == "{":
                # The UI sends modal fields as JSON in command_line
                data = json.loads(self.command_line)
                for k, v in data.items():
                    self.add_arg(k, v)
            else:
                parts = self.command_line.split(" ")
                if parts and parts[0]:
                    self.add_arg("module_name", parts[0])
        return None


class LoadCommand(CommandBase):
    cmd = "load"
    needs_admin = False
    help_cmd = "load <module_name>"
    description = "Load a COFF module (.o) into the agent"
    version = 1
    author = "@me"
    argument_class = LoadArguments
    attackmapping = []
    attributes = CommandAttributes(
        supported_os=[SupportedOS.Windows],
        builtin=True,
    )

    async def create_go_tasking(self, taskData: PTTaskMessageAllData) -> PTTaskCreateTaskingMessageResponse:
        response = PTTaskCreateTaskingMessageResponse(
            TaskID=taskData.Task.ID,
            Success=True,
        )

        file_id = taskData.args.get_arg("module_file")
        if not file_id:
            response.Success = False
            response.Error = "You must upload the .o file via the modal (module_file)"
            return response

        content_resp = await SendMythicRPCFileGetContent(
            MythicRPCFileGetContentMessage(AgentFileId=file_id)
        )
        if not content_resp.Success:
            response.Success = False
            response.Error = f"Failed to read the file: {content_resp.Error}"
            return response

        taskData.args.add_arg("module_file", base64.b64encode(content_resp.Content).decode("utf-8"))
        taskData.args.add_arg("module_name", taskData.args.get_arg("module_name") or "")

        # Mark as loaded on this callback: the newly loaded module AND the core
        # built-in commands (sleep/exit/load/... never go through load, but
        # the UI — File Browser / Process Browser — needs them as "loaded").
        CORE_COMMANDS = ["sleep", "exit", "load", "unload", "list", "download", "upload"]
        module_name = taskData.args.get_arg("module_name")
        commands_to_add = list(dict.fromkeys(CORE_COMMANDS + ([module_name] if module_name else [])))
        await SendMythicRPCCallbackAddCommand(MythicRPCCallbackAddCommandMessage(
            TaskID=taskData.Task.ID,
            Commands=commands_to_add,
        ))
        return response

    async def process_response(self, task: PTTaskMessageAllData, response: any) -> PTTaskProcessResponseMessageResponse:
        resp = PTTaskProcessResponseMessageResponse(TaskID=task.Task.ID, Success=True)
        return resp
