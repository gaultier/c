#include <_types/_uint8_t.h>
#define GB_IMPLEMENTATION
#include <assert.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/stab.h>
#include <stab.h>
#include <stdio.h>
#include <stdlib.h>

#include "../vendor/gb.h"

typedef struct __attribute__((packed)) {
    uint32_t length;
    uint16_t version;
    uint32_t header_length;
    uint8_t min_instruction_length;
    uint8_t max_ops_per_inst;
    uint8_t default_is_stmt;
    int8_t line_base;
    uint8_t line_range;
    uint8_t opcode_base;
    uint8_t std_opcode_lengths[12];
} dwarf_debug_line_header;

typedef enum : uint8_t {
    DW_LNS_extended_op = 0,
    DW_LNS_copy,
    DW_LNS_advance_pc,
    DW_LNS_advance_line,
    DW_LNS_set_file,
    DW_LNS_set_column,
    DW_LNS_negate_stmt,
    DW_LNS_set_basic_block,
    DW_LNS_const_add_pc,
    DW_LNS_fixed_advance_pc,
    DW_LNS_set_prologue_end,
    DW_LNS_set_epilogue_begin,
    DW_LNS_set_isa,
} DW_OP;

u64 read_leb128_encoded_unsigned(void* data, isize size, u64* offset) {
    u64 result = 0;
    u64 shift = 0;
    while (true) {
        u8* byte = &data[*offset];
        *offset += 1;
        u8 val = *byte;
        result |= (val & 0x7f) << shift;
        if ((val & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

int main(int argc, const char* argv[]) {
    assert(argc == 2);
    const char* path = argv[1];

    gbAllocator allocator = gb_heap_allocator();
    gbFileContents contents = gb_file_read_contents(allocator, true, path);

    u64 offset = 0;
    struct mach_header_64* h = &contents.data[offset];
    offset += sizeof(struct mach_header_64);
    assert(h->cputype == CPU_TYPE_X86_64);
    assert(h->filetype == MH_DSYM);

    printf(
        "magic=%d\ncputype=%d\ncpusubtype=%d\nfiletype=%d\nncmds=%"
        "d\nsizeofcmds=%d\nflags=%d\n",
        h->magic, h->cputype, h->cpusubtype, h->filetype, h->ncmds,
        h->sizeofcmds, h->flags);

    for (int cmd_count = 0; cmd_count < h->ncmds; cmd_count++) {
        struct load_command* c = &contents.data[offset];
        offset += sizeof(struct load_command);
        printf("command: cmd=%d cmdsize=%d\n", c->cmd, c->cmdsize);

        switch (c->cmd) {
            case LC_UUID: {
                struct uuid_command* uc =
                    &contents.data[offset - sizeof(struct load_command)];
                offset +=
                    sizeof(struct uuid_command) - sizeof(struct load_command);
                printf(
                    "LC_UUID uuid=%#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x "
                    "%#x %#x "
                    "%#x "
                    "%#x %#x\n",
                    uc->uuid[0], uc->uuid[1], uc->uuid[2], uc->uuid[3],
                    uc->uuid[4], uc->uuid[5], uc->uuid[6], uc->uuid[7],
                    uc->uuid[8], uc->uuid[9], uc->uuid[10], uc->uuid[11],
                    uc->uuid[12], uc->uuid[13], uc->uuid[14], uc->uuid[15]);

                break;
            }
            case LC_BUILD_VERSION: {
                struct build_version_command* vc =
                    &contents.data[offset - sizeof(struct load_command)];
                offset += sizeof(struct build_version_command) -
                          sizeof(struct load_command);
                printf(
                    "LC_BUILD_VERSION platform=%#x minos=%#x sdk=%#x "
                    "ntools=%d\n",
                    vc->platform, vc->minos, vc->sdk, vc->ntools);

                assert(vc->ntools == 0 && "UNIMPLEMENTED");
                break;
            }
            case LC_SYMTAB: {
                struct symtab_command* sc =
                    &contents.data[offset - sizeof(struct load_command)];
                offset +=
                    sizeof(struct symtab_command) - sizeof(struct load_command);

                printf("LC_SYMTAB symoff=%#x nsyms=%d stroff=%#x strsize=%d\n",
                       sc->symoff, sc->nsyms, sc->stroff, sc->strsize);

                // symbol table
                /* { */
                /*     const int pos = ftell(f); */
                /*     assert(fseek(f, sc.symoff, SEEK_SET) == 0); */
                /*     for (int sym_count = 0; sym_count < sc.nsyms;
                 * sym_count++) { */
                /*         struct nlist_64 nl = {0}; */
                /*         assert(fread(&nl, sizeof(nl), 1, f) >= 0); */
                /*         printf( */
                /*             "nlist_64 n_strx=%d n_type=%d n_sect=%d n_desc=%d
                 * " */
                /*             "n_value=%#llx\n", */
                /*             nl.n_un.n_strx, nl.n_type, nl.n_sect, nl.n_desc,
                 */
                /*             nl.n_value); */
                /*     } */
                /*     assert(fseek(f, pos, SEEK_SET) == 0); */
                /* } */
                // string table
                /* { */
                /*     const int pos = ftell(f); */
                /*     assert(fseek(f, sc.stroff, SEEK_SET) == 0); */
                /*     char* s = malloc(sc.strsize + 1); */
                /*     assert(fread(s, sc.strsize, 1, f) >= 0); */
                /*     printf("strings=%.*s\n", sc.strsize, s); */
                /*     assert(fseek(f, pos, SEEK_SET) == 0); */
                /* } */
                break;
            }
            case LC_SEGMENT_64: {
                struct segment_command_64* sc =
                    &contents.data[offset - sizeof(struct load_command)];
                offset += sizeof(struct segment_command_64) -
                          sizeof(struct load_command);

                printf(
                    "LC_SEGMENT_64 segname=%s vmaddr=%#llx vmsize=%#llx "
                    "fileoff=%#llx filesize=%#llx maxprot=%#x initprot=%#x "
                    "nsects=%d flags=%d\n",
                    sc->segname, sc->vmaddr, sc->vmsize, sc->fileoff,
                    sc->filesize, sc->maxprot, sc->initprot, sc->nsects,
                    sc->flags);

                for (int sec_count = 0; sec_count < sc->nsects; sec_count++) {
                    struct section_64* sec = &contents.data[offset];
                    offset += sizeof(struct section_64);
                    printf(
                        "SECTION sectname=%s segname=%s addr=%#llx size=%#llx "
                        "offset=%#x align=%#x reloff=%#x nreloc=%d flags=%#x\n",
                        sec->sectname, sec->segname, sec->addr, sec->size,
                        sec->offset, sec->align, sec->reloff, sec->nreloc,
                        sec->flags);

                    if (strcmp(sec->sectname, "__debug_line") == 0) {
                        u64 saved_offset = offset;
                        offset = sec->offset;
                        dwarf_debug_line_header* ddlh = &contents.data[offset];
                        offset += sizeof(dwarf_debug_line_header);
                        printf(
                            "DWARF length=%#x version=%#x header_length=%#x "
                            "min_instruction_length=%#x max_ops_per_inst=%d "
                            "default_is_stmt=%#x "
                            "line_base=%d "
                            "line_range=%d opcode_base=%d\n"
                            "DWARF std_opcode_lengths[0]=%d\n"
                            "DWARF std_opcode_lengths[1]=%d\n"
                            "DWARF std_opcode_lengths[2]=%d\n"
                            "DWARF std_opcode_lengths[3]=%d\n"
                            "DWARF std_opcode_lengths[4]=%d\n"
                            "DWARF std_opcode_lengths[5]=%d\n"
                            "DWARF std_opcode_lengths[6]=%d\n"
                            "DWARF std_opcode_lengths[7]=%d\n"
                            "DWARF std_opcode_lengths[8]=%d\n"
                            "DWARF std_opcode_lengths[9]=%d\n"
                            "DWARF std_opcode_lengths[10]=%d\n"
                            "DWARF std_opcode_lengths[11]=%d\n",
                            ddlh->length, ddlh->version, ddlh->header_length,
                            ddlh->min_instruction_length,
                            ddlh->max_ops_per_inst, ddlh->default_is_stmt,
                            ddlh->line_base, ddlh->line_range,
                            ddlh->opcode_base, ddlh->std_opcode_lengths[0],
                            ddlh->std_opcode_lengths[1],
                            ddlh->std_opcode_lengths[2],
                            ddlh->std_opcode_lengths[3],
                            ddlh->std_opcode_lengths[4],
                            ddlh->std_opcode_lengths[5],
                            ddlh->std_opcode_lengths[6],
                            ddlh->std_opcode_lengths[7],
                            ddlh->std_opcode_lengths[8],
                            ddlh->std_opcode_lengths[9],
                            ddlh->std_opcode_lengths[10],
                            ddlh->std_opcode_lengths[11]);

                        assert(ddlh->version == 4);
                        puts("Directories:");
                        while (offset < sec->offset + sec->size) {
                            char* s = &contents.data[offset];
                            char* end = memchr(&contents.data[offset], 0,
                                               contents.size - offset);
                            assert(end != NULL);
                            printf("- %s\n", s);
                            offset += end - s;
                            if (*(end + 1) == 0) {
                                offset += 2;
                                break;
                            }
                        }
                        puts("Files:");
                        while (offset < sec->offset + sec->size) {
                            char* s = &contents.data[offset];
                            if (*s == 0) {
                                offset += 1;
                                break;
                            }
                            char* end = memchr(&contents.data[offset], 0,
                                               contents.size - offset);
                            assert(end != NULL);
                            offset += end - s + 1;
                            u8* dir_index = &contents.data[offset++];
                            u8* modtime = &contents.data[offset++];
                            u8* length = &contents.data[offset++];
                            printf("- %s dir_index=%d modtime=%d length=%d\n",
                                   s, *dir_index, *modtime, *length);
                        }
                        puts("");

                        while (offset < sec->offset + sec->size) {
                            DW_OP* opcode = &contents.data[offset++];
                            printf("DW_OP=%d\n", *opcode);
                            switch (*opcode) {
                                case DW_LNS_extended_op: {
                                    const u64 decoded =
                                        read_leb128_encoded_unsigned(
                                            contents.data, contents.size,
                                            &offset);
                                    printf("DW_LNS_extended_op leb128=%#llx\n",
                                           decoded);
                                    break;
                                }
                                case DW_LNS_copy:
                                    break;
                                case DW_LNS_advance_pc: {
                                    const u64 decoded =
                                        read_leb128_encoded_unsigned(
                                            contents.data, contents.size,
                                            &offset);
                                    printf("DW_LNS_advance_pc leb128=%#llx\n",
                                           decoded);
                                    break;
                                }
                                case DW_LNS_advance_line:
                                    break;
                                case DW_LNS_set_file:
                                    break;
                                case DW_LNS_set_column:
                                    break;
                                case DW_LNS_negate_stmt:
                                    break;
                                case DW_LNS_set_basic_block:
                                    break;
                                case DW_LNS_const_add_pc:
                                    break;
                                case DW_LNS_fixed_advance_pc:
                                    break;
                                case DW_LNS_set_prologue_end:
                                    break;
                                case DW_LNS_set_epilogue_begin:
                                    break;
                                case DW_LNS_set_isa:
                                    break;
                                default:
                                    assert(0 && "UNIMPLEMENTED");
                            }
                        }

                        offset = saved_offset;
                    }
                }

                break;
            }
            default:
                assert(0 && "UNIMPLEMENTED - catch all");
        }
    }
}
