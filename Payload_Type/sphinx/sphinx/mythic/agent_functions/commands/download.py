from mythic_container.MythicCommandBase import *
from mythic_container.MythicRPC import *

class DownloadArguments(TaskArguments):
    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = [
            CommandParameter(
                name="path",
                type=ParameterType.String,
                description="Path to the file on the target (e.g. C:\\Users\\x\\file.txt)",
                parameter_group_info=[ParameterGroupInfo(ui_position=1)],
            ),
        ]

    async def parse_arguments(self):
        if len(self.command_line) > 0:
            if self.command_line[0] == "{":
                import json
                data = json.loads(self.command_line)
                for k, v in data.items():
                    self.add_arg(k, v)
            else:
                self.add_arg("path", self.command_line.strip())
        return None

class DownloadCommand(CommandBase):
    cmd = "download"
    needs_admin = False
    help_cmd = "download <path>"
    description = "Download a file from the target"
    version = 1
    author = "@me"
    argument_class = DownloadArguments
    attackmapping = []
    supported_ui_features=["file_browser:download"], # allows one-click download from the Mythic File Browser UI
    attributes = CommandAttributes(
        supported_os=[SupportedOS.Windows],
        builtin=True,            # core command, not a COFF module
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