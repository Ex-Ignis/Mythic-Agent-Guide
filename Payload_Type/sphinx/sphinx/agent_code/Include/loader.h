#ifndef SPHINX_LOADER_H
#define SPHINX_LOADER_H

#include <stdint.h>
#include <stddef.h>
#include "api_table.h"
#include "module.h"

/* Carga un archivo .o COFF en memoria, resuelve símbolos contra
   g_api y devuelve un Module* listo para registrar en la tabla.
   Devuelve: Module* (el caller hace module_register()) o NULL si falla.
*/

/* AMD64  should always be here */
#define MACHINETYPE_AMD64           0x8664

/* AMD64 Specific types */
#define IMAGE_REL_AMD64_ABSOLUTE    0x0000
#define IMAGE_REL_AMD64_ADDR64      0x0001
#define IMAGE_REL_AMD64_ADDR32NB    0x0003
/* Most common from the looks of it, just 32-bit relative address from the byte following the relocation */
#define IMAGE_REL_AMD64_REL32       0x0004
/* Second most common, 32-bit address without an image base. Not sure what that means... */
#define IMAGE_REL_AMD64_REL32_1     0x0005
#define IMAGE_REL_AMD64_REL32_2     0x0006
#define IMAGE_REL_AMD64_REL32_3     0x0007
#define IMAGE_REL_AMD64_REL32_4     0x0008
#define IMAGE_REL_AMD64_REL32_5     0x0009

/* Section Characteristic Flags */
#define IMAGE_SCN_MEM_EXECUTE       0x20000000
#define IMAGE_SCN_MEM_READ          0x40000000
#define IMAGE_SCN_MEM_WRITE         0x80000000
#define IMAGE_SCN_CNT_CODE          0x00000020
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080

/* sizeof 20 */
typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable; // relativo al inicio del archivo
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} coff_header_t;

/* Size of 40 */
typedef struct {
    char     Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress; // relativo casi siempre 0
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData; // relativo al inicio del archivo
    uint32_t PointerToRelocations; // relativo al inicio del archivo
    uint32_t PointerToLineNumbers; // relativo al inicio del archivo
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} coff_sect_t;

/* IMAGE_RELOCATION: 4 + 4 + 2 = 10 bytes EXACTOS (sin padding de alineacion) */
#pragma pack(push, 1)
typedef struct {
    uint32_t VirtualAddress; // relativo al INICIO DE SECCION
    uint32_t SymbolTableIndex;
    uint16_t Type;
} coff_reloc_t;
#pragma pack(pop)

/* IMAGE_SYMBOL: 8 + 4 + 2 + 2 + 1 + 1 = 18 bytes EXACTOS */
#pragma pack(push, 1)
typedef struct {
    union {
        char     Name[8];
        uint32_t value[2]; // first.value[1] -> relativo al INICIO DEL STRING TABLE
    } first;
    uint32_t Value; // relativo al INICIO DE SECCION indicado por SectionNumber
    uint16_t SectionNumber;
    uint16_t Type;
    uint8_t  StorageClass;
    uint8_t  NumberOfAuxSymbols;
} coff_sym_t;
#pragma pack(pop)

Module* coff_load(const uint8_t* raw_data, size_t size,
                  const char* name, uint8_t cmd_id, ApiTable* api);

#endif //SPHINX_LOADER_H
