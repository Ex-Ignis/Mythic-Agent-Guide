from mythic_container.MythicCommandBase import TaskArguments


class NoArgsArguments(TaskArguments):
    """TaskArguments vacia para comandos sin parametros estructurados.
    Mythic exige una argument_class no-None (llama a task.args.parse_arguments())."""

    def __init__(self, command_line, **kwargs):
        super().__init__(command_line, **kwargs)
        self.args = []

    async def parse_arguments(self):
        return None
