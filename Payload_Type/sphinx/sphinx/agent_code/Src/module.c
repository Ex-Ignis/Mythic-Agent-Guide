#include "../Include/module.h"

/**
 * find_module(cmd_id)      -- linear scan, returns Module* or NULL
 * module_register(mod)     -- finds free slot, copies struct, increments count
 * module_unregister(cmd_id)-- marks slot as not loaded, VirtualFrees code_base
 */
Module module_table[MAX_MODULES];
uint32_t module_count = 0;

int module_register(_In_ const Module* module) {
 // Overwrite/update existing entry if command_id already registered
 for (uint32_t i = 0; i < module_count; i++) {
  if (module_table[i].command_id == module->command_id) {
   // Clean up the old module before overwriting
   if (module_table[i].cleanup)
    module_table[i].cleanup(&g_api);

   if (module_table[i].code_base && module_table[i].code_size > 0)
    g_api.VirtualFree(module_table[i].code_base, 0, MEM_RELEASE);

   module_table[i] = *module;  // replace with new module
   return 0;
  }
 }

 if (module_count >= MAX_MODULES) return -1;

 module_table[module_count++] = *module;

 return 0;
}

int module_unregister(_In_ uint8_t command_id) {
 for (uint32_t i = 0; i < module_count; i++) {
  if (module_table[i].command_id == command_id && module_table[i].loaded) {
   if (module_table[i].cleanup)
    module_table[i].cleanup(&g_api);
   if (module_table[i].code_base && module_table[i].code_size > 0)
    g_api.VirtualFree(module_table[i].code_base, 0, MEM_RELEASE);

   // Swap last entry into the vacated slot (order-independent table)
   module_table[i] = module_table[module_count - 1];
   module_count--;
   return 0;
  }
 }
 return -1;
}

Module* find_module(uint8_t command_id) {
 for (uint32_t i = 0; i < module_count; i++) {
  if (module_table[i].command_id == command_id && module_table[i].loaded) {
   return &module_table[i];
  }
 }
  return NULL;
}
