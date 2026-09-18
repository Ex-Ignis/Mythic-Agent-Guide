import logging

from mythic_container.TranslationBase import TranslationContainer
from mythic_container.TranslationBase import (
    TrMythicC2ToCustomMessageFormatMessage,
    TrMythicC2ToCustomMessageFormatMessageResponse,
    TrCustomMessageToMythicC2FormatMessage,
    TrCustomMessageToMythicC2FormatMessageResponse,
)

from .utils import MYTHIC_CHECK_IN, MYTHIC_GET_TASKING
from .commands_from_c2 import checkin_to_agent_format, get_responses_to_agent_format
from .commands_from_implant import checkin_to_mythic_format, post_response_handler

logging.basicConfig(level=logging.INFO)
log = logging.getLogger(__name__)

class SphinxTranslator(TranslationContainer):
    name = "SphinxTranslator"
    description = "TLV binary translator for Sphinx agent"
    author = "@me"

    async def translate_to_c2_format(
        self, inputMsg: TrMythicC2ToCustomMessageFormatMessage
    ) -> TrMythicC2ToCustomMessageFormatMessageResponse:
        response = TrMythicC2ToCustomMessageFormatMessageResponse(Success=True)
        action = inputMsg.Message["action"]

        if action == "checkin":
            response.Message = checkin_to_agent_format(inputMsg.Message["id"])
        elif action == "get_tasking":
            response.Message = get_responses_to_agent_format(inputMsg)
        elif action == "post_response":
            # Mythic replies to agent post_response messages with file-transfer
            # acks (file_id, chunk_data). Convert them into synthetic
            # download_resp/upload_resp tasks for the agent.
            log.info("post_response action -> converting file acks to tasks")
            response.Message = get_responses_to_agent_format(inputMsg)
        else:
            log.warning(f"Unknown action: {action}")
            response.Message = b""

        return response

    async def translate_from_c2_format(
        self, inputMsg: TrCustomMessageToMythicC2FormatMessage
    ) -> TrCustomMessageToMythicC2FormatMessageResponse:
        response = TrCustomMessageToMythicC2FormatMessageResponse(Success=True)

        msg = inputMsg.Message
        msg_type = msg[0]
        msg_data = msg[1:]

        if msg_type == MYTHIC_CHECK_IN:
            response.Message = checkin_to_mythic_format(msg_data)
        elif msg_type == MYTHIC_GET_TASKING:
            response.Message = post_response_handler(msg_data)
        else:
            log.warning(f"Unknown message type: {hex(msg_type)}")
            response.Message = {"action": "get_tasking", "responses": []}

        return response
