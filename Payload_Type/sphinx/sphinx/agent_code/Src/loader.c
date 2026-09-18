#include "../Include/loader.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "api_table.h"
#include "module.h"

#define ALIGN_PAGE(x) (((x) + 0xFFF) & ~0xFFF)

// Search for a symbol by name ("go", "init" or "cleanup") in the COFF symbol table.
static coff_sym_t* coff_find_symbol(_In_ const uint8_t* raw_data, _In_ coff_header_t* hdr, _In_ const char* name) {

    // Symbol table
    coff_sym_t* sym_table = (coff_sym_t*) (raw_data + hdr->PointerToSymbolTable);

    // String table immediately follows the symbol table
    uint8_t* str_table_start = (uint8_t*) (raw_data + hdr->PointerToSymbolTable + hdr->NumberOfSymbols * sizeof(coff_sym_t));

    // First 4 bytes are the total string table size (including those 4 bytes).
    uint32_t strtab_size = 0;
    memcpy(&strtab_size, str_table_start, 4); // memcpy: string table may be unaligned
    if (strtab_size < 4) return NULL; // corrupt file
    const char* strtab = (const char*)(str_table_start + 4);

    for (uint32_t i = 0; i < hdr->NumberOfSymbols; i++) {
        coff_sym_t* sym = &sym_table[i];

        // Skip auxiliary symbol records
        if (sym->SectionNumber == 0 && sym->StorageClass == 0 && sym->Value == 0)
            continue;

        // Resolve symbol name
        const char* sym_name;

        if (sym->first.value[0] == 0) {
            if (sym->first.value[1] >= strtab_size - 4) // offset out of range
                continue; // corrupt symbol, skip
            // Long name: stored in the string table
            sym_name = strtab + sym->first.value[1];
        } else {
            // Short name: stored inline in the symbol entry (8 bytes)
            sym_name = sym->first.Name;
        }

        /* Short names (char[8]) may not be null-terminated;
           strncmp with limit 8 avoids reading past the symbol. */
        if (strncmp(sym_name, name, 8) == 0)
            return sym;
    }
    return NULL;
}

// Allocate all COFF sections in a single contiguous VirtualAlloc region.
// Each section is page-aligned within the region.
// base_out  = start of the region (for VirtualFree / code_base)
// total_out = total region size   (for VirtualFree / code_size)
static void** coff_alloc_sections(_In_ const uint8_t* raw_data, _In_ coff_header_t* hdr,
                                  _In_ ApiTable* api,
                                  _Out_ void** base_out, _Out_ size_t* total_out) {
    coff_sect_t* sects = (coff_sect_t*) (raw_data + sizeof(coff_header_t));

    void** section_map = (void**)api->malloc(sizeof(void*) * hdr->NumberOfSections);
    if (section_map == NULL) return NULL;
    api->memset(section_map, 0, sizeof(void*) * hdr->NumberOfSections);

    // 1. Calculate total size (sum of page-aligned section sizes)
    size_t total = 0;
    for (uint32_t i = 0; i < hdr->NumberOfSections; i++) {
        uint32_t alloc_size = (sects[i].SizeOfRawData > sects[i].VirtualSize)
                            ? sects[i].SizeOfRawData : sects[i].VirtualSize;
        total += ALIGN_PAGE(alloc_size);
    }
    if (total == 0) {
        api->free(section_map);
        return NULL;
    }

    // 2. Single VirtualAlloc for the whole image
    uint8_t* base = (uint8_t*)api->VirtualAlloc(NULL, total,
                                                MEM_COMMIT | MEM_RESERVE,
                                                PAGE_READWRITE);
    if (base == NULL) {
        api->free(section_map);
        return NULL;
    }

    // 3. Copy each section to its page-aligned offset within the region
    size_t cursor = 0;
    for (uint32_t i = 0; i < hdr->NumberOfSections; i++) {
        coff_sect_t* sect = &sects[i];
        uint32_t alloc_size = (sect->SizeOfRawData > sect->VirtualSize)
                            ? sect->SizeOfRawData : sect->VirtualSize;

        section_map[i] = base + cursor;

        if (sect->PointerToRawData != 0)
            api->memcpy(base + cursor, raw_data + sect->PointerToRawData, sect->SizeOfRawData);
        else
            api->memset(base + cursor, 0, alloc_size);

        cursor += ALIGN_PAGE(alloc_size);
    }

    *base_out  = base;
    *total_out = total;
    return section_map;
}

// Patch relocations for all sections.
// For each section, for each relocation entry:
//   1. Read the target symbol
//   2. If external and unresolved → error (not supported in Sphinx)
//   3. If local (defined in another section) → compute runtime address
//   4. Patch the site according to relocation type (REL32, ADDR64, ...)
static int coff_apply_relocs(_In_ const uint8_t *raw_data, _In_ coff_header_t *hdr,
                             _In_ void **section_map, _In_ coff_sym_t *syms,
                             _In_ void* image_base) {
    coff_sect_t *sects = (coff_sect_t *) (raw_data + sizeof(coff_header_t));
    for (uint32_t s = 0; s < hdr->NumberOfSections; s++) {
        coff_sect_t *sect = &sects[s];

        if (sect->NumberOfRelocations == 0) continue;
        coff_reloc_t *relocs = (coff_reloc_t *) (raw_data + sect->PointerToRelocations);

        for (uint32_t r = 0; r < sect->NumberOfRelocations; r++) {
            coff_reloc_t *reloc = &relocs[r];

            coff_sym_t *sym = &syms[reloc->SymbolTableIndex];

            void *target;
            if (sym->SectionNumber > 0) {
                // Local symbol
                target = (uint8_t *) section_map[sym->SectionNumber - 1] + sym->Value;
            } else {
                // External symbol → not supported in Sphinx
                return -1;
            }

            uint8_t *place = (uint8_t *) section_map[s] + reloc->VirtualAddress;

            switch (reloc->Type) {
                case IMAGE_REL_AMD64_REL32: { // PC-relative 32-bit call/jump
                    int32_t addend;
                    memcpy(&addend, place, 4); // read addend (usually 0)

                    int64_t displacement = (int64_t) target - (int64_t) (place + 4);
                    int32_t val = (int32_t) (addend + displacement);

                    memcpy(place, &val, 4);
                    break;
                }
                case IMAGE_REL_AMD64_ADDR64: { // absolute 64-bit address
                    int64_t addend64;
                    memcpy(&addend64, place, 8);
                    uint64_t val64 = (uint64_t) ((int64_t) target + addend64);
                    memcpy(place, &val64, 8);
                    break;
                }
                case IMAGE_REL_AMD64_ADDR32NB: { // RVA: offset relative to image base
                    int32_t addend;
                    memcpy(&addend, place, 4);
                    uint32_t rva = (uint32_t) ((uintptr_t) target - (uintptr_t) image_base) + addend;
                    memcpy(place, &rva, 4);
                    break;
                }
                case IMAGE_REL_AMD64_REL32_1:
                case IMAGE_REL_AMD64_REL32_2:
                case IMAGE_REL_AMD64_REL32_3:
                case IMAGE_REL_AMD64_REL32_4:
                case IMAGE_REL_AMD64_REL32_5: {
                    int extra_bytes = reloc->Type - IMAGE_REL_AMD64_REL32;
                    int32_t addend;
                    memcpy(&addend, place, 4);

                    // RIP will be at (place + 4 + extra_bytes)
                    int64_t displacement = (int64_t) target - (int64_t) (place + 4 + extra_bytes);
                    int32_t val = (int32_t) (addend + displacement);
                    memcpy(place, &val, 4);
                    break;
                }
                case IMAGE_REL_AMD64_ABSOLUTE:
                    break;   // no-op, skip
                default:
                    return -1;
            }
        }
    }
    return 0;
}

// Public entry point: orchestrates the full COFF load sequence.
Module* coff_load(const uint8_t* raw_data, size_t size,
                  const char* name, uint8_t cmd_id, ApiTable* api) {
    if (raw_data == NULL || size < sizeof(coff_header_t)) {
        fprintf(stderr, "[SPHINX] coff_load: data NULL or size < header (%d)\n", (int)size);
        return NULL;
    }

    // 1. Validate: machine must be 0x8664 (AMD64)
    coff_header_t* hdr = (coff_header_t*) raw_data;
    if (hdr->Machine != MACHINETYPE_AMD64) {
        fprintf(stderr, "[SPHINX] coff_load: machine=%04x (expected 0x8664)\n", hdr->Machine);
        return NULL;
    }
    if (hdr->PointerToSymbolTable >= size) {
        fprintf(stderr, "[SPHINX] coff_load: PointerToSymbolTable=%d >= size=%d\n",
                hdr->PointerToSymbolTable, (int)size);
        return NULL;
    }
    fprintf(stderr, "[SPHINX] coff_load: header ok, %d sections, %d symbols\n",
            hdr->NumberOfSections, hdr->NumberOfSymbols);

    coff_sym_t* syms = (coff_sym_t*) (raw_data + hdr->PointerToSymbolTable);

    // 2. coff_alloc_sections() → single VirtualAlloc + copy
    void*  image_base  = NULL;
    size_t image_size  = 0;
    void** section_map = coff_alloc_sections(raw_data, hdr, api, &image_base, &image_size);
    if (section_map == NULL) {
        fprintf(stderr, "[SPHINX] coff_load: alloc_sections failed\n");
        return NULL;
    }
    fprintf(stderr, "[SPHINX] coff_load: sections allocated (%d bytes)\n", (int)image_size);

    // 3. coff_apply_relocs() → patch internal references
    if (coff_apply_relocs(raw_data, hdr, section_map, syms, image_base) != 0) {
        fprintf(stderr, "[SPHINX] coff_load: apply_relocs failed (external symbol?)\n");
        goto cleanup;
    }
    fprintf(stderr, "[SPHINX] coff_load: relocs ok\n");

    // 4. coff_find_symbol("go") → entry point
    coff_sym_t* go_sym = coff_find_symbol(raw_data, hdr, "go");
    if (go_sym == NULL || go_sym->SectionNumber == 0) {
        fprintf(stderr, "[SPHINX] coff_load: symbol 'go' not found\n");
        goto cleanup;
    }
    fprintf(stderr, "[SPHINX] coff_load: 'go' in section %d, value=%d\n",
            go_sym->SectionNumber, go_sym->Value);

    // 5-6. init / cleanup (optional)
    coff_sym_t* init_sym    = coff_find_symbol(raw_data, hdr, "init");
    coff_sym_t* cleanup_sym = coff_find_symbol(raw_data, hdr, "cleanup");

    // 7. Fill Module struct
    Module* mod = (Module*) api->malloc(sizeof(Module));
    if (mod == NULL) goto cleanup;
    api->memset(mod, 0, sizeof(Module));

    mod->command_id = cmd_id;

    size_t name_len = api->strlen(name);
    if (name_len >= MODULE_NAME_LEN) name_len = MODULE_NAME_LEN - 1;
    api->memcpy(mod->name, name, name_len);
    mod->name[name_len] = '\0';

    mod->loaded = 1;
    mod->run = (void (*)(ApiTable*, const char*, Param*, uint32_t))
               ((uint8_t*) section_map[go_sym->SectionNumber - 1] + go_sym->Value);

    if (init_sym != NULL && init_sym->SectionNumber > 0)
        mod->init = (void (*)(ApiTable*))
                    ((uint8_t*) section_map[init_sym->SectionNumber - 1] + init_sym->Value);

    if (cleanup_sym != NULL && cleanup_sym->SectionNumber > 0)
        mod->cleanup = (void (*)(ApiTable*))
                       ((uint8_t*) section_map[cleanup_sym->SectionNumber - 1] + cleanup_sym->Value);

    mod->code_base = image_base;
    mod->code_size = image_size;

    // 8. Apply final page permissions per section (RX for code, RW/RO for data)
    coff_sect_t* sects = (coff_sect_t*) (raw_data + sizeof(coff_header_t));
    for (uint32_t i = 0; i < hdr->NumberOfSections; i++) {
        uint32_t alloc_size = (sects[i].SizeOfRawData > sects[i].VirtualSize)
                            ? sects[i].SizeOfRawData : sects[i].VirtualSize;

        DWORD prot;
        if (sects[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
            prot = PAGE_EXECUTE_READ;
        else if (sects[i].Characteristics & IMAGE_SCN_MEM_WRITE)
            prot = PAGE_READWRITE;
        else
            prot = PAGE_READONLY;

        DWORD old;
        api->VirtualProtect(section_map[i], alloc_size, prot, &old);
    }

    // 9. Call init if the module provides one
    if (mod->init)
        mod->init(api);

    api->free(section_map);
    return mod;

cleanup:
    if (image_base)
        api->VirtualFree(image_base, 0, MEM_RELEASE);
    api->free(section_map);
    return NULL;
}
