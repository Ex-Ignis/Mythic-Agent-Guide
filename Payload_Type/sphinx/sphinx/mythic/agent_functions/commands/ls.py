import json

from mythic_container.MythicCommandBase import *
from mythic_container.MythicRPC import *


class LsArguments(TaskArguments):
    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = [
            CommandParameter(
                name="path",
                type=ParameterType.String,
                description="Directory to list (default: current directory)",
                parameter_group_info=[ParameterGroupInfo(required=False, ui_position=1)],
            ),
        ]

    async def parse_arguments(self):
        if len(self.command_line) == 0:
            return None
        if self.command_line[0] == "{":
            try:
                data = json.loads(self.command_line)
            except Exception:
                return None
            # Tasking from the Mythic File Browser: {host, path, file, full_path}
            if data.get("full_path"):
                self.add_arg("path", data["full_path"])
            else:
                p = data.get("path") or ""
                f = data.get("file") or ""
                joined = (p.rstrip("\\/") + "\\" + f) if (p and f) else (p or f)
                if joined:
                    self.add_arg("path", joined)
        else:
            self.add_arg("path", self.command_line.strip())
        return None


class LsCommand(CommandBase):
    cmd = "ls"
    needs_admin = False
    help_cmd = "ls [path]"
    description = "List files in a directory"
    version = 1
    author = "@me"
    argument_class = LsArguments
    attackmapping = []
    supported_ui_features = ["file_browser:list"]
    attributes = CommandAttributes(
        supported_os=[SupportedOS.Windows],
        builtin=False,
        suggested_command=True,
    )

    async def create_go_tasking(self, taskData: PTTaskMessageAllData) -> PTTaskCreateTaskingMessageResponse:
        response = PTTaskCreateTaskingMessageResponse(
            TaskID=taskData.Task.ID,
            Success=True,
        )
        return response

    async def process_response(self, task: PTTaskMessageAllData, response: any) -> PTTaskProcessResponseMessageResponse:
        resp = PTTaskProcessResponseMessageResponse(TaskID=task.Task.ID, Success=True)
        return resp
