import logging

from mythic_container.PayloadBuilder import BuildResponse, BuildStatus

log = logging.getLogger(__name__)


async def build(self) -> BuildResponse:
    resp = BuildResponse(status=BuildStatus.Success)
    resp.build_message = "Sphinx build stub - agent code not yet compiled"
    return resp
