from mythic_container.MythicCommandBase import *
from mythic_container.MythicRPC import *
from .no_args import NoArgsArguments


class NetCommand(CommandBase):
    cmd = "net"
    needs_admin = False
    help_cmd = "net"
    description = "List network information about the system"
    version = 1
    author = "@me"
    argument_class = NoArgsArguments
    attackmapping = []
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
