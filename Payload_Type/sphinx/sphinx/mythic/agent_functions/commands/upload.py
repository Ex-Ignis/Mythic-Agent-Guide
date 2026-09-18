from mythic_container.MythicCommandBase import *
from mythic_container.MythicRPC import *

class UploadArguments(TaskArguments):
    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = [
            CommandParameter(
                name="file",
                type=ParameterType.File,
                description="File to upload (stored in Mythic, returns a file_id)",
                parameter_group_info=[ParameterGroupInfo(ui_position=1)],
            ),
            CommandParameter(
                name="path",
                type=ParameterType.String,
                description="Destination path on the target (e.g. C:\\Temp\\tool.exe)",
                parameter_group_info=[ParameterGroupInfo(ui_position=2)],
            ),
        ]

    async def parse_arguments(self):
        if len(self.command_line) > 0 and self.command_line[0] == "{":
            import json
            data = json.loads(self.command_line)
            for k, v in data.items():
                self.add_arg(k, v)
        return None


class UploadCommand(CommandBase):
    cmd = "upload"
    needs_admin = False
    help_cmd = "upload (modal: file + path)"
    description = "Upload a file to the target machine"
    version = 1
    author = "@me"
    argument_class = UploadArguments
    attackmapping = []
    supported_ui_features=["file_browser:upload"], # allows one-click upload from the Mythic File Browser UI
    attributes = CommandAttributes(
        supported_os=[SupportedOS.Windows],
        builtin=True,
        suggested_command=True,
    )

    async def create_go_tasking(self, taskData: PTTaskMessageAllData) -> PTTaskCreateTaskingMessageResponse:
        response = PTTaskCreateTaskingMessageResponse(
            TaskID=taskData.Task.ID,
            Success=True,
        )

        # 'file' is its file_id (UUID).
        # Pass it to the agent together with the destination path. Order defines params[0]/params[1].
        file_id = taskData.args.get_arg("file")
        dest = taskData.args.get_arg("path")
        if not file_id or not dest:
            response.Success = False
            response.Error = "Missing file or path"
            return response

        taskData.args.add_arg("file", file_id)
        taskData.args.add_arg("path", dest)
        return response

    async def process_response(self, task: PTTaskMessageAllData, response: any) -> PTTaskProcessResponseMessageResponse:
        resp = PTTaskProcessResponseMessageResponse(TaskID=task.Task.ID, Success=True)
        return resp
