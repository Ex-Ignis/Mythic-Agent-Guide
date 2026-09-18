from mythic_container.MythicCommandBase import *
from mythic_container.MythicRPC import *


class ShellResizeArguments(TaskArguments):
    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = [
            CommandParameter(
                name="size",
                type=ParameterType.String,
                description='New terminal dimensions as "cols rows" (e.g. "120 40")',
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
                self.add_arg("size", self.command_line.strip())
        return None


class ShellResizeCommand(CommandBase):
    cmd = "shell_resize"
    needs_admin = False
    help_cmd = "shell_resize <cols> <rows>"
    description = "Resize the ConPTY window of the running shell (e.g. shell_resize 120 40)"
    version = 1
    author = "@me"
    argument_class = ShellResizeArguments
    attackmapping = []
    attributes = CommandAttributes(
        supported_os=[SupportedOS.Windows],
        builtin=False,
        suggested_command=False,
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
