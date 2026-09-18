from mythic_container.MythicCommandBase import *
from mythic_container.MythicRPC import *
from .no_args import NoArgsArguments


class ExitCommand(CommandBase):
    cmd = "exit"
    needs_admin = False
    help_cmd = "exit"
    description = "Terminate the agent process"
    version = 1
    author = "@me"
    argument_class = NoArgsArguments
    attackmapping = []
    supported_ui_features = ["callback_table:exit"] # se usara para el exit de mythic
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
        return response

    async def process_response(self, task: PTTaskMessageAllData, response: any) -> PTTaskProcessResponseMessageResponse:
        resp = PTTaskProcessResponseMessageResponse(TaskID=task.Task.ID, Success=True)
        return resp
