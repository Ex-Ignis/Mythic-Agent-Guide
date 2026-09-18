#ifndef SPHINX_PROTOCOL_H
#define SPHINX_PROTOCOL_H

/*
 * Sphinx protocol message types — single source of truth (C side).
 * Must match translator/utils.py exactly.
 *
 * Convention: family 'A' = agent control, family 'D' = data / file transfers.
 *
 *   MSG_CHECKIN         (agent -> C2)  initial check-in
 *   MSG_GET_TASKING     (agent -> C2)  task request (outer type byte on GET/POST)
 *   MSG_TASK_RESPONSE   (agent -> C2)  task result
 *   MSG_DOWNLOAD_INIT   (agent -> C2)  start a download (registers the file)
 *   MSG_DOWNLOAD_CONT   (agent -> C2)  download chunk
 *   MSG_UPLOAD_PULL     (agent -> C2)  request the next upload chunk
 *   MSG_SHELL_DATA      (agent -> C2)  interactive shell output
 *   MSG_SOCKS_DATA      (both)         SOCKS data (future)
 *   MSG_FILE_BROWSER    (agent -> C2)  directory listing (future)
 */

#define MSG_CHECKIN         0xA1
#define MSG_GET_TASKING     0xA2
#define MSG_TASK_RESPONSE   0xA4

#define MSG_DOWNLOAD_INIT   0xD1
#define MSG_DOWNLOAD_CONT   0xD2
#define MSG_UPLOAD_PULL     0xD3

#define MSG_SHELL_DATA      0xDA

#define MSG_SOCKS_DATA      0xD8
#define MSG_FILE_BROWSER    0xD9

#endif //SPHINX_PROTOCOL_H
