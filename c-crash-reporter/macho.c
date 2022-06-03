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

static u64 read_leb128_u64(void* data, u64* offset) {
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

static i64 read_leb128_s64(void* data, u64* offset) {
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
    DW_TAG_null = 0x0000,
    DW_TAG_array_type = 0x0001,
    DW_TAG_class_type = 0x0002,
    DW_TAG_entry_point = 0x0003,
    DW_TAG_enumeration_type = 0x0004,
    DW_TAG_formal_parameter = 0x0005,
    DW_TAG_imported_declaration = 0x0008,
    DW_TAG_label = 0x000a,
    DW_TAG_lexical_block = 0x000b,
    DW_TAG_member = 0x000d,
    DW_TAG_pointer_type = 0x000f,
    DW_TAG_reference_type = 0x0010,
    DW_TAG_compile_unit = 0x0011,
    DW_TAG_string_type = 0x0012,
    DW_TAG_structure_type = 0x0013,
    DW_TAG_subroutine_type = 0x0015,
    DW_TAG_typedef = 0x0016,
    DW_TAG_union_type = 0x0017,
    DW_TAG_unspecified_parameters = 0x0018,
    DW_TAG_variant = 0x0019,
    DW_TAG_common_block = 0x001a,
    DW_TAG_common_inclusion = 0x001b,
    DW_TAG_inheritance = 0x001c,
    DW_TAG_inlined_subroutine = 0x001d,
    DW_TAG_module = 0x001e,
    DW_TAG_ptr_to_member_type = 0x001f,
    DW_TAG_set_type = 0x0020,
    DW_TAG_subrange_type = 0x0021,
    DW_TAG_with_stmt = 0x0022,
    DW_TAG_access_declaration = 0x0023,
    DW_TAG_base_type = 0x0024,
    DW_TAG_catch_block = 0x0025,
    DW_TAG_const_type = 0x0026,
    DW_TAG_constant = 0x0027,
    DW_TAG_enumerator = 0x0028,
    DW_TAG_file_type = 0x0029,
    DW_TAG_friend = 0x002a,
    DW_TAG_namelist = 0x002b,
    DW_TAG_namelist_item = 0x002c,
    DW_TAG_packed_type = 0x002d,
    DW_TAG_subprogram = 0x002e,
    DW_TAG_template_type_parameter = 0x002f,
    DW_TAG_template_value_parameter = 0x0030,
    DW_TAG_thrown_type = 0x0031,
    DW_TAG_try_block = 0x0032,
    DW_TAG_variant_part = 0x0033,
    DW_TAG_variable = 0x0034,
    DW_TAG_volatile_type = 0x0035,
    DW_TAG_dwarf_procedure = 0x0036,
    DW_TAG_restrict_type = 0x0037,
    DW_TAG_interface_type = 0x0038,
    DW_TAG_namespace = 0x0039,
    DW_TAG_imported_module = 0x003a,
    DW_TAG_unspecified_type = 0x003b,
    DW_TAG_partial_unit = 0x003c,
    DW_TAG_imported_unit = 0x003d,
    DW_TAG_condition = 0x003f,
    DW_TAG_shared_type = 0x0040,
    DW_TAG_type_unit = 0x0041,
    DW_TAG_rvalue_reference_type = 0x0042,
    DW_TAG_template_alias = 0x0043,
} dw_tag;

typedef enum : uint8_t {
    DW_AT_sibling = 0x01,
    DW_AT_location = 0x02,
    DW_AT_name = 0x03,
    DW_AT_ordering = 0x09,
    DW_AT_byte_size = 0x0b,
    DW_AT_bit_offset = 0x0c,
    DW_AT_bit_size = 0x0d,
    DW_AT_stmt_list = 0x10,
    DW_AT_low_pc = 0x11,
    DW_AT_high_pc = 0x12,
    DW_AT_language = 0x13,
    DW_AT_discr = 0x15,
    DW_AT_discr_value = 0x16,
    DW_AT_visibility = 0x17,
    DW_AT_import = 0x18,
    DW_AT_string_length = 0x19,
    DW_AT_common_reference = 0x1a,
    DW_AT_comp_dir = 0x1b,
    DW_AT_const_value = 0x1c,
    DW_AT_containing_type = 0x1d,
    DW_AT_default_value = 0x1e,
    DW_AT_inline = 0x20,
    DW_AT_is_optional = 0x21,
    DW_AT_lower_bound = 0x22,
    DW_AT_producer = 0x25,
    DW_AT_prototyped = 0x27,
    DW_AT_return_addr = 0x2a,
    DW_AT_start_scope = 0x2c,
    DW_AT_bit_stride = 0x2e,
    DW_AT_upper_bound = 0x2f,
    DW_AT_abstract_origin = 0x31,
    DW_AT_accessibility = 0x32,
    DW_AT_address_class = 0x33,
    DW_AT_artificial = 0x34,
    DW_AT_base_types = 0x35,
    DW_AT_calling_convention = 0x36,
    DW_AT_count = 0x37,
    DW_AT_data_member_location = 0x38,
    DW_AT_decl_column = 0x39,
    DW_AT_decl_file = 0x3a,
    DW_AT_decl_line = 0x3b,
    DW_AT_declaration = 0x3c,
    DW_AT_discr_list = 0x3d,
    DW_AT_encoding = 0x3e,
    DW_AT_external = 0x3f,
    DW_AT_frame_base = 0x40,
    DW_AT_friend = 0x41,
    DW_AT_identifier_case = 0x42,
    DW_AT_macro_info = 0x43,
    DW_AT_namelist_item = 0x44,
    DW_AT_priority = 0x45,
    DW_AT_segment = 0x46,
    DW_AT_specification = 0x47,
    DW_AT_static_link = 0x48,
    DW_AT_type = 0x49,
    DW_AT_use_location = 0x4a,
    DW_AT_variable_parameter = 0x4b,
    DW_AT_virtuality = 0x4c,
    DW_AT_vtable_elem_location = 0x4d,
    DW_AT_allocated = 0x4e,
    DW_AT_associated = 0x4f,
    DW_AT_data_location = 0x50,
    DW_AT_byte_stride = 0x51,
    DW_AT_entry_pc = 0x52,
    DW_AT_use_UTF8 = 0x53,
    DW_AT_extension = 0x54,
    DW_AT_ranges = 0x55,
    DW_AT_trampoline = 0x56,
    DW_AT_call_column = 0x57,
    DW_AT_call_file = 0x58,
    DW_AT_call_line = 0x59,
    DW_AT_description = 0x5a,
    DW_AT_binary_scale = 0x5b,
    DW_AT_decimal_scale = 0x5c,
    DW_AT_small = 0x5d,
    DW_AT_decimal_sign = 0x5e,
    DW_AT_digit_count = 0x5f,
    DW_AT_picture_string = 0x60,
    DW_AT_mutable = 0x61,
    DW_AT_threads_scaled = 0x62,
    DW_AT_explicit = 0x63,
    DW_AT_object_pointer = 0x64,
    DW_AT_endianity = 0x65,
    DW_AT_elemental = 0x66,
    DW_AT_pure = 0x67,
    DW_AT_recursive = 0x68,
    DW_AT_signature = 0x69,
    DW_AT_main_subprogram = 0x6a,
    DW_AT_data_bit_offset = 0x6b,
    DW_AT_const_expr = 0x6c,
    DW_AT_enum_class = 0x6d,
    DW_AT_linkage_name = 0x6e,
} dw_attribute;

typedef enum : uint8_t {
    DW_FORM_addr = 0x01,
    DW_FORM_block2 = 0x03,
    DW_FORM_block4 = 0x04,
    DW_FORM_data2 = 0x05,
    DW_FORM_data4 = 0x06,
    DW_FORM_data8 = 0x07,
    DW_FORM_string = 0x08,
    DW_FORM_block = 0x09,
    DW_FORM_block1 = 0x0a,
    DW_FORM_data1 = 0x0b,
    DW_FORM_flag = 0x0c,
    DW_FORM_sdata = 0x0d,
    DW_FORM_strp = 0x0e,
    DW_FORM_udata = 0x0f,
    DW_FORM_ref_addr = 0x10,
    DW_FORM_ref1 = 0x11,
    DW_FORM_ref2 = 0x12,
    DW_FORM_ref4 = 0x13,
    DW_FORM_ref8 = 0x14,
    DW_FORM_ref_udata = 0x15,
    DW_FORM_indirect = 0x16,
    DW_FORM_sec_offset = 0x17,
    DW_FORM_exprloc = 0x18,
    DW_FORM_flag_present = 0x19,
    DW_FORM_ref_sig8 = 0x20,
} dw_form;

typedef enum : uint8_t {
    DW_LNE_end_sequence = 1,
    DW_LNE_set_address,
    DW_LNE_define_file,
    DW_LNE_set_discriminator,
} DW_LNE;

typedef struct {
} dwarf_info;

static void read_dwarf_ext_op(void* data, isize size, u64* offset, u64* address,
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

static void read_dwarf_section_debug_abbrev(void* data,
                                            const struct section_64* sec) {
    u64 offset = sec->offset;
    while (offset < sec->offset + sec->size) {
        u8* type_num = &data[offset++];
        if (*type_num == 0) break;

        u8* tag = &data[offset++];
        bool* has_children = &data[offset++];
        printf(".debug_abbrev: type_num=%d tag=%#x has_children=%d\n",
               *type_num, *tag, *has_children);

        while (offset < sec->offset + sec->size) {
            u8* attr = &data[offset++];
            u8* form = &data[offset++];
            if (*attr == 0 && *form == 0) break;
            printf(".debug_abbrev: attr=%#x form=%#x\n", *attr, *form);
        }
    }
}

static void read_dwarf_section_debug_info(void* data,
                                          const struct section_64* sec) {
    u64 offset = sec->offset;

    u32* size = &data[offset];
    offset += sizeof(u32);
    printf(".debug_info size=%#x\n", *size);

    u16* version = &data[offset];
    offset += sizeof(u16);
    printf(".debug_info version=%u\n", *version);
    assert(*version == 4);

    u32* abbr_offset = &data[offset];
    offset += sizeof(u32);
    printf(".debug_info abbr_offset=%#x\n", *abbr_offset);

    u8* addr_size = &data[offset];
    offset += sizeof(u8);
    printf(".debug_info abbr_size=%#x\n", *addr_size);

    // TODO
}

void read_dwarf_section_debug_str(void* data, const struct section_64* sec) {
    u64 offset = sec->offset;
    u64 i = 0;
    while (offset < sec->offset + sec->size) {
        char* s = &data[offset];
        if (*s == 0) {
            offset++;
            continue;
        }

        char* end = memchr(&data[offset], 0, sec->offset + sec->size);
        assert(end != NULL);
        printf("- [%llu] %s\n", i, s);
        offset += end - s;
        i++;
    }
}

static void read_dwarf_section_debug_line(void* data,
                                          const struct section_64* sec) {
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

static void read_macho_dsym(void* data, isize size) {
    u64 offset = 0;
    const struct mach_header_64* h = &data[offset];
    offset += sizeof(struct mach_header_64);
    assert(h->cputype == CPU_TYPE_X86_64);
    assert(h->filetype == MH_DSYM);

    printf(
        "magic=%d\ncputype=%d\ncpusubtype=%d\nfiletype=%d\nncmds=%"
        "d\nsizeofcmds=%d\nflags=%d\n",
        h->magic, h->cputype, h->cpusubtype, h->filetype, h->ncmds,
        h->sizeofcmds, h->flags);

    for (int cmd_count = 0; cmd_count < h->ncmds; cmd_count++) {
        const struct load_command* c = &data[offset];
        offset += sizeof(struct load_command);
        printf("command: cmd=%d cmdsize=%d\n", c->cmd, c->cmdsize);

        switch (c->cmd) {
            case LC_UUID: {
                const struct uuid_command* uc =
                    &data[offset - sizeof(struct load_command)];
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
                const struct build_version_command* vc =
                    &data[offset - sizeof(struct load_command)];
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
                const struct symtab_command* sc =
                    &data[offset - sizeof(struct load_command)];
                offset +=
                    sizeof(struct symtab_command) - sizeof(struct load_command);

                printf("LC_SYMTAB symoff=%#x nsyms=%d stroff=%#x strsize=%d\n",
                       sc->symoff, sc->nsyms, sc->stroff, sc->strsize);

                break;
            }
            case LC_SEGMENT_64: {
                const struct segment_command_64* sc =
                    &data[offset - sizeof(struct load_command)];
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
                    const struct section_64* sec = &data[offset];
                    offset += sizeof(struct section_64);
                    printf(
                        "SECTION sectname=%s segname=%s addr=%#llx size=%#llx "
                        "offset=%#x align=%#x reloff=%#x nreloc=%d flags=%#x\n",
                        sec->sectname, sec->segname, sec->addr, sec->size,
                        sec->offset, sec->align, sec->reloff, sec->nreloc,
                        sec->flags);

                    if (strcmp(sec->sectname, "__debug_line") == 0) {
                        read_dwarf_section_debug_line(data, sec);
                    } else if (strcmp(sec->sectname, "__debug_str") == 0) {
                        read_dwarf_section_debug_str(data, sec);
                    } else if (strcmp(sec->sectname, "__debug_info") == 0) {
                        read_dwarf_section_debug_info(data, sec);
                    } else if (strcmp(sec->sectname, "__debug_abbrev") == 0) {
                        read_dwarf_section_debug_abbrev(data, sec);
                    }
                }

                break;
            }
            default:
                assert(0 && "UNIMPLEMENTED - catch all");
        }
    }
}

int main(int argc, const char* argv[]) {
    assert(argc == 2);
    const char* path = argv[1];

    gbAllocator allocator = gb_heap_allocator();
    gbFileContents contents = gb_file_read_contents(allocator, true, path);

    read_macho_dsym(contents.data, contents.size);
}
