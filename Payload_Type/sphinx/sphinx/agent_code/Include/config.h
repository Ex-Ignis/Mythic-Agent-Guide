/*
 * config.h — build-time configuration (stamped by builder.py at payload build time).
 *
 * This file ships with placeholder values so the repo compiles out of the box.
 * Replace the values below for local development builds, or let the Mythic
 * builder stamp them automatically when building through the UI.
 *
 * NEVER commit real IPs, ports, or AES keys to a public repository.
 */

#ifndef SPHINX_CONFIG_H
#define SPHINX_CONFIG_H

#define CALLBACK_HOST  L"127.0.0.1"          // replace with your Mythic server IP/host
#define CALLBACK_PORT  80                     // replace with your listener port
#define CALLBACK_PATH  L"/index"             // GET URI (raw_c2_config get.uris)
#define POST_PATH      L"/data"              // POST URI (raw_c2_config post.uris)
#define USER_AGENT     L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/105.0.0.0 Safari/537.36"
#define SLEEP_TIME     10
#define JITTER         10
#define DEBUG          1
#define USE_TLS        0                      // set to 1 when callback_domains uses https://

/* Payload UUID — must match the UUID Mythic assigned to your payload build.
   The builder stamps this automatically; replace for manual local builds. */
#define PAYLOAD_UUID   "00000000-0000-0000-0000-000000000000"

/* AES-256 key — stamped by the builder from the payload's AESPSK.enc_key.
   This placeholder key is zeroed and will NOT decrypt real Mythic traffic. */
#define ENC_KEY {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,\
                 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}

#endif // SPHINX_CONFIG_H
