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

u64 read_leb128_u64(void* data, u64* offset) {
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

i64 read_leb128_s64(void* data, u64* offset) {
    i64 result = 0;
    u64 shift = 0;
    u8 val = 0;
    const u64 bit_count = sizeof(i64) * 8;
    do {
        u8* byte = &data[*offset];
        *offset += 1;
        val = *byte;
        result |= (val & 0x7f) << shift;
        shift += 7;
    } while (((val & 0x80) != 0));

    /* sign bit of byte is second high-order bit (0x40) */
    if (shift < bit_count && ((val & 0x40) != 0)) {
        /* sign extend */
        result |= (~0 << shift);
    }

    return result;
}

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
} DW_LNS;

typedef enum : uint8_t {
    DW_LNE_end_sequence = 1,
    DW_LNE_set_address,
    DW_LNE_define_file,
    DW_LNE_set_discriminator,
} DW_LNE;

typedef struct {
} dwarf_info;

void read_dwarf_ext_op(void* data, isize size, u64* offset, u64* address,
                       int* file) {
    const u64 start_offset = *offset;
    DW_LNE* extended_opcode = &data[*offset];
    *offset += 1;
    printf("DW_EXT_OP=%d\n", *extended_opcode);

    switch (*extended_opcode) {
        case DW_LNE_end_sequence: {
            puts("DW_LNE_end_sequence");
            break;
        }
        case DW_LNE_set_address: {
            const u64* a = &data[*offset];
            *offset += sizeof(u64);
            printf("DW_LNE_set_address addr=%#llx\n", *a);
            *address = *a;
            break;
        }
        case DW_LNE_define_file: {
            break;
        }
        case DW_LNE_set_discriminator: {
            break;
        }
        default:
            assert(0 && "UNREACHABLE");
    }
    while (*offset - start_offset < size) *offset += 1;  // Skip rest
}

void read_dwarf_section_debug_str(void* data, u64 end_offset, u64* offset) {}

void read_dwarf_section_debug_line(void* data, struct section_64* sec) {
    u64 offset = sec->offset;
    dwarf_debug_line_header* ddlh = &data[offset];
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
        ddlh->min_instruction_length, ddlh->max_ops_per_inst,
        ddlh->default_is_stmt, ddlh->line_base, ddlh->line_range,
        ddlh->opcode_base, ddlh->std_opcode_lengths[0],
        ddlh->std_opcode_lengths[1], ddlh->std_opcode_lengths[2],
        ddlh->std_opcode_lengths[3], ddlh->std_opcode_lengths[4],
        ddlh->std_opcode_lengths[5], ddlh->std_opcode_lengths[6],
        ddlh->std_opcode_lengths[7], ddlh->std_opcode_lengths[8],
        ddlh->std_opcode_lengths[9], ddlh->std_opcode_lengths[10],
        ddlh->std_opcode_lengths[11]);

    assert(ddlh->version == 4);
    puts("Directories:");
    while (offset < sec->offset + sec->size) {
        char* s = &data[offset];
        char* end = memchr(&data[offset], 0, sec->offset + sec->size);
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
        char* s = &data[offset];
        if (*s == 0) {
            offset += 1;
            break;
        }
        char* end = memchr(&data[offset], 0, sec->offset + sec->size);
        assert(end != NULL);
        offset += end - s + 1;
        u64 dir_index = read_leb128_u64(data, &offset);
        u64 modtime = read_leb128_u64(data, &offset);

        u64 length = read_leb128_u64(data, &offset);

        printf(
            "- %s dir_index=%llu modtime=%llu "
            "length=%llu\n",
            s, dir_index, modtime, length);
    }
    puts("");

    // FSM
    u64 address = 0;
    u64 line = 0;
    // TODO: track column?
    int /* FIXME */ file = 0;
    bool is_stmt = false;

    while (offset < sec->offset + sec->size) {
        DW_LNS* opcode = &data[offset];
        offset += 1;
        printf("DW_OP=%#x offset=%#llx address=%#llx line=%lld\n", *opcode,
               offset, address, line + 1);
        switch (*opcode) {
            case DW_LNS_extended_op: {
                const u64 size = read_leb128_u64(data, &offset);
                printf("DW_LNS_extended_op size=%#llx\n", size);

                read_dwarf_ext_op(data, size, &offset, &address, &file);
                break;
            }
            case DW_LNS_copy:
                puts("DW_LNS_copy");
                break;
            case DW_LNS_advance_pc: {
                const u64 decoded = read_leb128_u64(data, &offset);
                printf("DW_LNS_advance_pc leb128=%#llx\n", decoded);
                address += decoded;
                break;
            }
            case DW_LNS_advance_line: {
                const u64 l = read_leb128_s64(data, &offset);
                printf("DW_LNS_advance_line line=%lld\n", l);
                line = l;
                break;
            }
            case DW_LNS_set_file:
                break;
            case DW_LNS_set_column: {
                const u64 column = read_leb128_u64(data, &offset);
                printf("DW_LNS_set_column column=%llu\n", column);
                break;
            }
            case DW_LNS_negate_stmt:
                puts("DW_LNS_negate_stmt");
                is_stmt = !is_stmt;
                break;
            case DW_LNS_set_basic_block:
                break;
            case DW_LNS_const_add_pc: {
                const u8 op = 255 - ddlh->opcode_base;
                address += op / ddlh->line_range * ddlh->min_instruction_length;
                // TODO: op_index
                printf("address+=%#x -> address=%#llx\n",
                       op / ddlh->line_range * ddlh->min_instruction_length,
                       address);
                break;
            }
            case DW_LNS_fixed_advance_pc:
                break;
            case DW_LNS_set_prologue_end:
                puts("DW_LNS_set_prologue_end");
                break;
            case DW_LNS_set_epilogue_begin:
                break;
            case DW_LNS_set_isa:
                break;
            default: {
                const u8 op = *opcode - ddlh->opcode_base;
                address += op / ddlh->line_range * ddlh->min_instruction_length;
                line += ddlh->line_base + op % ddlh->line_range;
                printf("address+=%d line+=%d\n",
                       op / ddlh->line_range * ddlh->min_instruction_length,
                       ddlh->line_base + op % ddlh->line_range);
            }
        }
    }
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
                        read_dwarf_section_debug_line(contents.data, sec);
                    }
                }

                break;
            }
            default:
                assert(0 && "UNIMPLEMENTED - catch all");
        }
    }
}
