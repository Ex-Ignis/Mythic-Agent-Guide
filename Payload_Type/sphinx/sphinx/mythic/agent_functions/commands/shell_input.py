from mythic_container.MythicCommandBase import *
from mythic_container.MythicRPC import *


class ShellInputArguments(TaskArguments):
    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = [
            CommandParameter(
                name="input",
                type=ParameterType.String,
                description="Text to send to the shell's stdin (newline appended automatically)",
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
                self.add_arg("input", self.command_line)
        return None


class ShellInputCommand(CommandBase):
    cmd = "shell_input"
    needs_admin = False
    help_cmd = "shell_input <text>"
    description = "Send input to the running interactive shell"
    version = 1
    author = "@me"
    argument_class = ShellInputArguments
    attackmapping = []
    attributes = CommandAttributes(
        supported_os=[SupportedOS.Windows],
        builtin=False,
        suggested_command=False,
    )

    async def create_go_tasking(self, taskData: PTTaskMessageAllData) -> PTTaskCreateTaskingMessageResponse:
        cmd_text = taskData.args.get_arg("input")
        if cmd_text and not cmd_text.endswith("\n"):
            taskData.args.set_arg("input", cmd_text + "\r\n")
        response = PTTaskCreateTaskingMessageResponse(
            TaskID=taskData.Task.ID,
            Success=True,
        )
        return response

    async def process_response(self, task: PTTaskMessageAllData, response: any) -> PTTaskProcessResponseMessageResponse:
        resp = PTTaskProcessResponseMessageResponse(TaskID=task.Task.ID, Success=True)
        return resp
