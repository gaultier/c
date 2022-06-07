#define GB_IMPLEMENTATION
#include <assert.h>
#include <mach-o/loader.h>
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

typedef enum : uint16_t {
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

const char dw_tag_str[][40] = {
    [DW_TAG_null] = "DW_TAG_null",
    [DW_TAG_array_type] = "DW_TAG_array_type",
    [DW_TAG_class_type] = "DW_TAG_class_type",
    [DW_TAG_entry_point] = "DW_TAG_entry_point",
    [DW_TAG_enumeration_type] = "DW_TAG_enumeration_type",
    [DW_TAG_formal_parameter] = "DW_TAG_formal_parameter",
    [DW_TAG_imported_declaration] = "DW_TAG_imported_declaration",
    [DW_TAG_label] = "DW_TAG_label",
    [DW_TAG_lexical_block] = "DW_TAG_lexical_block",
    [DW_TAG_member] = "DW_TAG_member",
    [DW_TAG_pointer_type] = "DW_TAG_pointer_type",
    [DW_TAG_reference_type] = "DW_TAG_reference_type",
    [DW_TAG_compile_unit] = "DW_TAG_compile_unit",
    [DW_TAG_string_type] = "DW_TAG_string_type",
    [DW_TAG_structure_type] = "DW_TAG_structure_type",
    [DW_TAG_subroutine_type] = "DW_TAG_subroutine_type",
    [DW_TAG_typedef] = "DW_TAG_typedef",
    [DW_TAG_union_type] = "DW_TAG_union_type",
    [DW_TAG_unspecified_parameters] = "DW_TAG_unspecified_parameters",
    [DW_TAG_variant] = "DW_TAG_variant",
    [DW_TAG_common_block] = "DW_TAG_common_block",
    [DW_TAG_common_inclusion] = "DW_TAG_common_inclusion",
    [DW_TAG_inheritance] = "DW_TAG_inheritance",
    [DW_TAG_inlined_subroutine] = "DW_TAG_inlined_subroutine",
    [DW_TAG_module] = "DW_TAG_module",
    [DW_TAG_ptr_to_member_type] = "DW_TAG_ptr_to_member_type",
    [DW_TAG_set_type] = "DW_TAG_set_type",
    [DW_TAG_subrange_type] = "DW_TAG_subrange_type",
    [DW_TAG_with_stmt] = "DW_TAG_with_stmt",
    [DW_TAG_access_declaration] = "DW_TAG_access_declaration",
    [DW_TAG_base_type] = "DW_TAG_base_type",
    [DW_TAG_catch_block] = "DW_TAG_catch_block",
    [DW_TAG_const_type] = "DW_TAG_const_type",
    [DW_TAG_constant] = "DW_TAG_constant",
    [DW_TAG_enumerator] = "DW_TAG_enumerator",
    [DW_TAG_file_type] = "DW_TAG_file_type",
    [DW_TAG_friend] = "DW_TAG_friend",
    [DW_TAG_namelist] = "DW_TAG_namelist",
    [DW_TAG_namelist_item] = "DW_TAG_namelist_item",
    [DW_TAG_packed_type] = "DW_TAG_packed_type",
    [DW_TAG_subprogram] = "DW_TAG_subprogram",
    [DW_TAG_template_type_parameter] = "DW_TAG_template_type_parameter",
    [DW_TAG_template_value_parameter] = "DW_TAG_template_value_parameter",
    [DW_TAG_thrown_type] = "DW_TAG_thrown_type",
    [DW_TAG_try_block] = "DW_TAG_try_block",
    [DW_TAG_variant_part] = "DW_TAG_variant_part",
    [DW_TAG_variable] = "DW_TAG_variable",
    [DW_TAG_volatile_type] = "DW_TAG_volatile_type",
    [DW_TAG_dwarf_procedure] = "DW_TAG_dwarf_procedure",
    [DW_TAG_restrict_type] = "DW_TAG_restrict_type",
    [DW_TAG_interface_type] = "DW_TAG_interface_type",
    [DW_TAG_namespace] = "DW_TAG_namespace",
    [DW_TAG_imported_module] = "DW_TAG_imported_module",
    [DW_TAG_unspecified_type] = "DW_TAG_unspecified_type",
    [DW_TAG_partial_unit] = "DW_TAG_partial_unit",
    [DW_TAG_imported_unit] = "DW_TAG_imported_unit",
    [DW_TAG_condition] = "DW_TAG_condition",
    [DW_TAG_shared_type] = "DW_TAG_shared_type",
    [DW_TAG_type_unit] = "DW_TAG_type_unit",
    [DW_TAG_rvalue_reference_type] = "DW_TAG_rvalue_reference_type",
    [DW_TAG_template_alias] = "DW_TAG_template_alias",
};

typedef enum : uint16_t {
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
    DW_AT_MIPS_loop_begin = 0x2002,
    DW_AT_MIPS_tail_loop_begin = 0x2003,
    DW_AT_MIPS_epilog_begin = 0x2004,
    DW_AT_MIPS_loop_unroll_factor = 0x2005,
    DW_AT_MIPS_software_pipeline_depth = 0x2006,
    DW_AT_MIPS_linkage_name = 0x2007,
    DW_AT_MIPS_stride = 0x2008,
    DW_AT_MIPS_abstract_name = 0x2009,
    DW_AT_MIPS_clone_origin = 0x200a,
    DW_AT_MIPS_has_inlines = 0x200b,
    DW_AT_MIPS_stride_byte = 0x200c,
    DW_AT_MIPS_stride_elem = 0x200d,
    DW_AT_MIPS_ptr_dopetype = 0x200e,
    DW_AT_MIPS_allocatable_dopetype = 0x200f,
    DW_AT_MIPS_assumed_shape_dopetype = 0x2010,
    DW_AT_MIPS_assumed_size = 0x2011,
    DW_AT_sf_names = 0x2101,
    DW_AT_src_info = 0x2102,
    DW_AT_mac_info = 0x2103,
    DW_AT_src_coords = 0x2104,
    DW_AT_body_begin = 0x2105,
    DW_AT_body_end = 0x2106,
    DW_AT_GNU_vector = 0x2107,
    DW_AT_GNU_template_name = 0x2110,
    DW_AT_GNU_odr_signature = 0x210f,
    DW_AT_GNU_call_site_value = 0x2111,
    DW_AT_GNU_call_site_data_value = 0x2112,
    DW_AT_GNU_call_site_target = 0x2113,
    DW_AT_GNU_call_site_target_clobbered = 0x2114,
    DW_AT_GNU_tail_call = 0x2115,
    DW_AT_GNU_all_tail_call_sites = 0x2116,
    DW_AT_GNU_all_call_sites = 0x2117,
    DW_AT_GNU_all_source_call_sites = 0x2118,
    DW_AT_GNU_macros = 0x2119,
    DW_AT_GNU_dwo_name = 0x2130,
    DW_AT_GNU_dwo_id = 0x2131,
    DW_AT_GNU_ranges_base = 0x2132,
    DW_AT_GNU_addr_base = 0x2133,
    DW_AT_GNU_pubnames = 0x2134,
    DW_AT_GNU_pubtypes = 0x2135,
    DW_AT_GNU_discriminator = 0x2136,
    DW_AT_BORLAND_property_read = 0x3b11,
    DW_AT_BORLAND_property_write = 0x3b12,
    DW_AT_BORLAND_property_implements = 0x3b13,
    DW_AT_BORLAND_property_index = 0x3b14,
    DW_AT_BORLAND_property_default = 0x3b15,
    DW_AT_BORLAND_Delphi_unit = 0x3b20,
    DW_AT_BORLAND_Delphi_class = 0x3b21,
    DW_AT_BORLAND_Delphi_record = 0x3b22,
    DW_AT_BORLAND_Delphi_metaclass = 0x3b23,
    DW_AT_BORLAND_Delphi_constructor = 0x3b24,
    DW_AT_BORLAND_Delphi_destructor = 0x3b25,
    DW_AT_BORLAND_Delphi_anonymous_method = 0x3b26,
    DW_AT_BORLAND_Delphi_interface = 0x3b27,
    DW_AT_BORLAND_Delphi_ABI = 0x3b28,
    DW_AT_BORLAND_Delphi_return = 0x3b29,
    DW_AT_BORLAND_Delphi_frameptr = 0x3b30,
    DW_AT_BORLAND_closure = 0x3b31,
    DW_AT_LLVM_include_path = 0x3e00,
    DW_AT_LLVM_config_macros = 0x3e01,
    DW_AT_LLVM_sysroot = 0x3e02,
    DW_AT_LLVM_tag_offset = 0x3e03,
    DW_AT_LLVM_apinotes = 0x3e07,
    DW_AT_APPLE_optimized = 0x3fe1,
    DW_AT_APPLE_flags = 0x3fe2,
    DW_AT_APPLE_isa = 0x3fe3,
    DW_AT_APPLE_block = 0x3fe4,
    DW_AT_APPLE_major_runtime_vers = 0x3fe5,
    DW_AT_APPLE_runtime_class = 0x3fe6,
    DW_AT_APPLE_omit_frame_ptr = 0x3fe7,
    DW_AT_APPLE_property_name = 0x3fe8,
    DW_AT_APPLE_property_getter = 0x3fe9,
    DW_AT_APPLE_property_setter = 0x3fea,
    DW_AT_APPLE_property_attribute = 0x3feb,
    DW_AT_APPLE_objc_complete_type = 0x3fec,
    DW_AT_APPLE_property = 0x3fed,
    DW_AT_APPLE_objc_direct = 0x3fee,
    DW_AT_APPLE_sdk = 0x3fef,
} dw_attribute;

void dw_attribute_to_str(dw_attribute attr, char* s, usize size) {
    assert(s != NULL);
    assert(size >= 50);

    switch (attr) {
        case DW_AT_sibling:
            memcpy(s, "DW_AT_sibling", sizeof("DW_AT_sibling"));
            break;
        case DW_AT_location:
            memcpy(s, "DW_AT_location", sizeof("DW_AT_location"));
            break;
        case DW_AT_name:
            memcpy(s, "DW_AT_name", sizeof("DW_AT_name"));
            break;
        case DW_AT_ordering:
            memcpy(s, "DW_AT_ordering", sizeof("DW_AT_ordering"));
            break;
        case DW_AT_byte_size:
            memcpy(s, "DW_AT_byte_size", sizeof("DW_AT_byte_size"));
            break;
        case DW_AT_bit_offset:
            memcpy(s, "DW_AT_bit_offset", sizeof("DW_AT_bit_offset"));
            break;
        case DW_AT_bit_size:
            memcpy(s, "DW_AT_bit_size", sizeof("DW_AT_bit_size"));
            break;
        case DW_AT_stmt_list:
            memcpy(s, "DW_AT_stmt_list", sizeof("DW_AT_stmt_list"));
            break;
        case DW_AT_low_pc:
            memcpy(s, "DW_AT_low_pc", sizeof("DW_AT_low_pc"));
            break;
        case DW_AT_high_pc:
            memcpy(s, "DW_AT_high_pc", sizeof("DW_AT_high_pc"));
            break;
        case DW_AT_language:
            memcpy(s, "DW_AT_language", sizeof("DW_AT_language"));
            break;
        case DW_AT_discr:
            memcpy(s, "DW_AT_discr", sizeof("DW_AT_discr"));
            break;
        case DW_AT_discr_value:
            memcpy(s, "DW_AT_discr_value", sizeof("DW_AT_discr_value"));
            break;
        case DW_AT_visibility:
            memcpy(s, "DW_AT_visibility", sizeof("DW_AT_visibility"));
            break;
        case DW_AT_import:
            memcpy(s, "DW_AT_import", sizeof("DW_AT_import"));
            break;
        case DW_AT_string_length:
            memcpy(s, "DW_AT_string_length", sizeof("DW_AT_string_length"));
            break;
        case DW_AT_common_reference:
            memcpy(s, "DW_AT_common_reference",
                   sizeof("DW_AT_common_reference"));
            break;
        case DW_AT_comp_dir:
            memcpy(s, "DW_AT_comp_dir", sizeof("DW_AT_comp_dir"));
            break;
        case DW_AT_const_value:
            memcpy(s, "DW_AT_const_value", sizeof("DW_AT_const_value"));
            break;
        case DW_AT_containing_type:
            memcpy(s, "DW_AT_containing_type", sizeof("DW_AT_containing_type"));
            break;
        case DW_AT_default_value:
            memcpy(s, "DW_AT_default_value", sizeof("DW_AT_default_value"));
            break;
        case DW_AT_inline:
            memcpy(s, "DW_AT_inline", sizeof("DW_AT_inline"));
            break;
        case DW_AT_is_optional:
            memcpy(s, "DW_AT_is_optional", sizeof("DW_AT_is_optional"));
            break;
        case DW_AT_lower_bound:
            memcpy(s, "DW_AT_lower_bound", sizeof("DW_AT_lower_bound"));
            break;
        case DW_AT_producer:
            memcpy(s, "DW_AT_producer", sizeof("DW_AT_producer"));
            break;
        case DW_AT_prototyped:
            memcpy(s, "DW_AT_prototyped", sizeof("DW_AT_prototyped"));
            break;
        case DW_AT_return_addr:
            memcpy(s, "DW_AT_return_addr", sizeof("DW_AT_return_addr"));
            break;
        case DW_AT_start_scope:
            memcpy(s, "DW_AT_start_scope", sizeof("DW_AT_start_scope"));
            break;
        case DW_AT_bit_stride:
            memcpy(s, "DW_AT_bit_stride", sizeof("DW_AT_bit_stride"));
            break;
        case DW_AT_upper_bound:
            memcpy(s, "DW_AT_upper_bound", sizeof("DW_AT_upper_bound"));
            break;
        case DW_AT_abstract_origin:
            memcpy(s, "DW_AT_abstract_origin", sizeof("DW_AT_abstract_origin"));
            break;
        case DW_AT_accessibility:
            memcpy(s, "DW_AT_accessibility", sizeof("DW_AT_accessibility"));
            break;
        case DW_AT_address_class:
            memcpy(s, "DW_AT_address_class", sizeof("DW_AT_address_class"));
            break;
        case DW_AT_artificial:
            memcpy(s, "DW_AT_artificial", sizeof("DW_AT_artificial"));
            break;
        case DW_AT_base_types:
            memcpy(s, "DW_AT_base_types", sizeof("DW_AT_base_types"));
            break;
        case DW_AT_calling_convention:
            memcpy(s, "DW_AT_calling_convention",
                   sizeof("DW_AT_calling_convention"));
            break;
        case DW_AT_count:
            memcpy(s, "DW_AT_count", sizeof("DW_AT_count"));
            break;
        case DW_AT_data_member_location:
            memcpy(s, "DW_AT_data_member_location",
                   sizeof("DW_AT_data_member_location"));
            break;
        case DW_AT_decl_column:
            memcpy(s, "DW_AT_decl_column", sizeof("DW_AT_decl_column"));
            break;
        case DW_AT_decl_file:
            memcpy(s, "DW_AT_decl_file", sizeof("DW_AT_decl_file"));
            break;
        case DW_AT_decl_line:
            memcpy(s, "DW_AT_decl_line", sizeof("DW_AT_decl_line"));
            break;
        case DW_AT_declaration:
            memcpy(s, "DW_AT_declaration", sizeof("DW_AT_declaration"));
            break;
        case DW_AT_discr_list:
            memcpy(s, "DW_AT_discr_list", sizeof("DW_AT_discr_list"));
            break;
        case DW_AT_encoding:
            memcpy(s, "DW_AT_encoding", sizeof("DW_AT_encoding"));
            break;
        case DW_AT_external:
            memcpy(s, "DW_AT_external", sizeof("DW_AT_external"));
            break;
        case DW_AT_frame_base:
            memcpy(s, "DW_AT_frame_base", sizeof("DW_AT_frame_base"));
            break;
        case DW_AT_friend:
            memcpy(s, "DW_AT_friend", sizeof("DW_AT_friend"));
            break;
        case DW_AT_identifier_case:
            memcpy(s, "DW_AT_identifier_case", sizeof("DW_AT_identifier_case"));
            break;
        case DW_AT_macro_info:
            memcpy(s, "DW_AT_macro_info", sizeof("DW_AT_macro_info"));
            break;
        case DW_AT_namelist_item:
            memcpy(s, "DW_AT_namelist_item", sizeof("DW_AT_namelist_item"));
            break;
        case DW_AT_priority:
            memcpy(s, "DW_AT_priority", sizeof("DW_AT_priority"));
            break;
        case DW_AT_segment:
            memcpy(s, "DW_AT_segment", sizeof("DW_AT_segment"));
            break;
        case DW_AT_specification:
            memcpy(s, "DW_AT_specification", sizeof("DW_AT_specification"));
            break;
        case DW_AT_static_link:
            memcpy(s, "DW_AT_static_link", sizeof("DW_AT_static_link"));
            break;
        case DW_AT_type:
            memcpy(s, "DW_AT_type", sizeof("DW_AT_type"));
            break;
        case DW_AT_use_location:
            memcpy(s, "DW_AT_use_location", sizeof("DW_AT_use_location"));
            break;
        case DW_AT_variable_parameter:
            memcpy(s, "DW_AT_variable_parameter",
                   sizeof("DW_AT_variable_parameter"));
            break;
        case DW_AT_virtuality:
            memcpy(s, "DW_AT_virtuality", sizeof("DW_AT_virtuality"));
            break;
        case DW_AT_vtable_elem_location:
            memcpy(s, "DW_AT_vtable_elem_location",
                   sizeof("DW_AT_vtable_elem_location"));
            break;
        case DW_AT_allocated:
            memcpy(s, "DW_AT_allocated", sizeof("DW_AT_allocated"));
            break;
        case DW_AT_associated:
            memcpy(s, "DW_AT_associated", sizeof("DW_AT_associated"));
            break;
        case DW_AT_data_location:
            memcpy(s, "DW_AT_data_location", sizeof("DW_AT_data_location"));
            break;
        case DW_AT_byte_stride:
            memcpy(s, "DW_AT_byte_stride", sizeof("DW_AT_byte_stride"));
            break;
        case DW_AT_entry_pc:
            memcpy(s, "DW_AT_entry_pc", sizeof("DW_AT_entry_pc"));
            break;
        case DW_AT_use_UTF8:
            memcpy(s, "DW_AT_use_UTF8", sizeof("DW_AT_use_UTF8"));
            break;
        case DW_AT_extension:
            memcpy(s, "DW_AT_extension", sizeof("DW_AT_extension"));
            break;
        case DW_AT_ranges:
            memcpy(s, "DW_AT_ranges", sizeof("DW_AT_ranges"));
            break;
        case DW_AT_trampoline:
            memcpy(s, "DW_AT_trampoline", sizeof("DW_AT_trampoline"));
            break;
        case DW_AT_call_column:
            memcpy(s, "DW_AT_call_column", sizeof("DW_AT_call_column"));
            break;
        case DW_AT_call_file:
            memcpy(s, "DW_AT_call_file", sizeof("DW_AT_call_file"));
            break;
        case DW_AT_call_line:
            memcpy(s, "DW_AT_call_line", sizeof("DW_AT_call_line"));
            break;
        case DW_AT_description:
            memcpy(s, "DW_AT_description", sizeof("DW_AT_description"));
            break;
        case DW_AT_binary_scale:
            memcpy(s, "DW_AT_binary_scale", sizeof("DW_AT_binary_scale"));
            break;
        case DW_AT_decimal_scale:
            memcpy(s, "DW_AT_decimal_scale", sizeof("DW_AT_decimal_scale"));
            break;
        case DW_AT_small:
            memcpy(s, "DW_AT_small", sizeof("DW_AT_small"));
            break;
        case DW_AT_decimal_sign:
            memcpy(s, "DW_AT_decimal_sign", sizeof("DW_AT_decimal_sign"));
            break;
        case DW_AT_digit_count:
            memcpy(s, "DW_AT_digit_count", sizeof("DW_AT_digit_count"));
            break;
        case DW_AT_picture_string:
            memcpy(s, "DW_AT_picture_string", sizeof("DW_AT_picture_string"));
            break;
        case DW_AT_mutable:
            memcpy(s, "DW_AT_mutable", sizeof("DW_AT_mutable"));
            break;
        case DW_AT_threads_scaled:
            memcpy(s, "DW_AT_threads_scaled", sizeof("DW_AT_threads_scaled"));
            break;
        case DW_AT_explicit:
            memcpy(s, "DW_AT_explicit", sizeof("DW_AT_explicit"));
            break;
        case DW_AT_object_pointer:
            memcpy(s, "DW_AT_object_pointer", sizeof("DW_AT_object_pointer"));
            break;
        case DW_AT_endianity:
            memcpy(s, "DW_AT_endianity", sizeof("DW_AT_endianity"));
            break;
        case DW_AT_elemental:
            memcpy(s, "DW_AT_elemental", sizeof("DW_AT_elemental"));
            break;
        case DW_AT_pure:
            memcpy(s, "DW_AT_pure", sizeof("DW_AT_pure"));
            break;
        case DW_AT_recursive:
            memcpy(s, "DW_AT_recursive", sizeof("DW_AT_recursive"));
            break;
        case DW_AT_signature:
            memcpy(s, "DW_AT_signature", sizeof("DW_AT_signature"));
            break;
        case DW_AT_main_subprogram:
            memcpy(s, "DW_AT_main_subprogram", sizeof("DW_AT_main_subprogram"));
            break;
        case DW_AT_data_bit_offset:
            memcpy(s, "DW_AT_data_bit_offset", sizeof("DW_AT_data_bit_offset"));
            break;
        case DW_AT_const_expr:
            memcpy(s, "DW_AT_const_expr", sizeof("DW_AT_const_expr"));
            break;
        case DW_AT_enum_class:
            memcpy(s, "DW_AT_enum_class", sizeof("DW_AT_enum_class"));
            break;
        case DW_AT_linkage_name:
            memcpy(s, "DW_AT_linkage_name", sizeof("DW_AT_linkage_name"));
            break;
        case DW_AT_MIPS_loop_begin:
            memcpy(s, "DW_AT_MIPS_loop_begin", sizeof("DW_AT_MIPS_loop_begin"));
            break;
        case DW_AT_MIPS_tail_loop_begin:
            memcpy(s, "DW_AT_MIPS_tail_loop_begin",
                   sizeof("DW_AT_MIPS_tail_loop_begin"));
            break;
        case DW_AT_MIPS_epilog_begin:
            memcpy(s, "DW_AT_MIPS_epilog_begin",
                   sizeof("DW_AT_MIPS_epilog_begin"));
            break;
        case DW_AT_MIPS_loop_unroll_factor:
            memcpy(s, "DW_AT_MIPS_loop_unroll_factor",
                   sizeof("DW_AT_MIPS_loop_unroll_factor"));
            break;
        case DW_AT_MIPS_software_pipeline_depth:
            memcpy(s, "DW_AT_MIPS_software_pipeline_depth",
                   sizeof("DW_AT_MIPS_software_pipeline_depth"));
            break;
        case DW_AT_MIPS_linkage_name:
            memcpy(s, "DW_AT_MIPS_linkage_name",
                   sizeof("DW_AT_MIPS_linkage_name"));
            break;
        case DW_AT_MIPS_stride:
            memcpy(s, "DW_AT_MIPS_stride", sizeof("DW_AT_MIPS_stride"));
            break;
        case DW_AT_MIPS_abstract_name:
            memcpy(s, "DW_AT_MIPS_abstract_name",
                   sizeof("DW_AT_MIPS_abstract_name"));
            break;
        case DW_AT_MIPS_clone_origin:
            memcpy(s, "DW_AT_MIPS_clone_origin",
                   sizeof("DW_AT_MIPS_clone_origin"));
            break;
        case DW_AT_MIPS_has_inlines:
            memcpy(s, "DW_AT_MIPS_has_inlines",
                   sizeof("DW_AT_MIPS_has_inlines"));
            break;
        case DW_AT_MIPS_stride_byte:
            memcpy(s, "DW_AT_MIPS_stride_byte",
                   sizeof("DW_AT_MIPS_stride_byte"));
            break;
        case DW_AT_MIPS_stride_elem:
            memcpy(s, "DW_AT_MIPS_stride_elem",
                   sizeof("DW_AT_MIPS_stride_elem"));
            break;
        case DW_AT_MIPS_ptr_dopetype:
            memcpy(s, "DW_AT_MIPS_ptr_dopetype",
                   sizeof("DW_AT_MIPS_ptr_dopetype"));
            break;
        case DW_AT_MIPS_allocatable_dopetype:
            memcpy(s, "DW_AT_MIPS_allocatable_dopetype",
                   sizeof("DW_AT_MIPS_allocatable_dopetype"));
            break;
        case DW_AT_MIPS_assumed_shape_dopetype:
            memcpy(s, "DW_AT_MIPS_assumed_shape_dopetype",
                   sizeof("DW_AT_MIPS_assumed_shape_dopetype"));
            break;
        case DW_AT_MIPS_assumed_size:
            memcpy(s, "DW_AT_MIPS_assumed_size",
                   sizeof("DW_AT_MIPS_assumed_size"));
            break;
        case DW_AT_sf_names:
            memcpy(s, "DW_AT_sf_names", sizeof("DW_AT_sf_names"));
            break;
        case DW_AT_src_info:
            memcpy(s, "DW_AT_src_info", sizeof("DW_AT_src_info"));
            break;
        case DW_AT_mac_info:
            memcpy(s, "DW_AT_mac_info", sizeof("DW_AT_mac_info"));
            break;
        case DW_AT_src_coords:
            memcpy(s, "DW_AT_src_coords", sizeof("DW_AT_src_coords"));
            break;
        case DW_AT_body_begin:
            memcpy(s, "DW_AT_body_begin", sizeof("DW_AT_body_begin"));
            break;
        case DW_AT_body_end:
            memcpy(s, "DW_AT_body_end", sizeof("DW_AT_body_end"));
            break;
        case DW_AT_GNU_vector:
            memcpy(s, "DW_AT_GNU_vector", sizeof("DW_AT_GNU_vector"));
            break;
        case DW_AT_GNU_template_name:
            memcpy(s, "DW_AT_GNU_template_name",
                   sizeof("DW_AT_GNU_template_name"));
            break;
        case DW_AT_GNU_odr_signature:
            memcpy(s, "DW_AT_GNU_odr_signature",
                   sizeof("DW_AT_GNU_odr_signature"));
            break;
        case DW_AT_GNU_call_site_value:
            memcpy(s, "DW_AT_GNU_call_site_value",
                   sizeof("DW_AT_GNU_call_site_value"));
            break;
        case DW_AT_GNU_call_site_data_value:
            memcpy(s, "DW_AT_GNU_call_site_data_value",
                   sizeof("DW_AT_GNU_call_site_data_value"));
            break;
        case DW_AT_GNU_call_site_target:
            memcpy(s, "DW_AT_GNU_call_site_target",
                   sizeof("DW_AT_GNU_call_site_target"));
            break;
        case DW_AT_GNU_call_site_target_clobbered:
            memcpy(s, "DW_AT_GNU_call_site_target_clobbered",
                   sizeof("DW_AT_GNU_call_site_target_clobbered"));
            break;
        case DW_AT_GNU_tail_call:
            memcpy(s, "DW_AT_GNU_tail_call", sizeof("DW_AT_GNU_tail_call"));
            break;
        case DW_AT_GNU_all_tail_call_sites:
            memcpy(s, "DW_AT_GNU_all_tail_call_sites",
                   sizeof("DW_AT_GNU_all_tail_call_sites"));
            break;
        case DW_AT_GNU_all_call_sites:
            memcpy(s, "DW_AT_GNU_all_call_sites",
                   sizeof("DW_AT_GNU_all_call_sites"));
            break;
        case DW_AT_GNU_all_source_call_sites:
            memcpy(s, "DW_AT_GNU_all_source_call_sites",
                   sizeof("DW_AT_GNU_all_source_call_sites"));
            break;
        case DW_AT_GNU_macros:
            memcpy(s, "DW_AT_GNU_macros", sizeof("DW_AT_GNU_macros"));
            break;
        case DW_AT_GNU_dwo_name:
            memcpy(s, "DW_AT_GNU_dwo_name", sizeof("DW_AT_GNU_dwo_name"));
            break;
        case DW_AT_GNU_dwo_id:
            memcpy(s, "DW_AT_GNU_dwo_id", sizeof("DW_AT_GNU_dwo_id"));
            break;
        case DW_AT_GNU_ranges_base:
            memcpy(s, "DW_AT_GNU_ranges_base", sizeof("DW_AT_GNU_ranges_base"));
            break;
        case DW_AT_GNU_addr_base:
            memcpy(s, "DW_AT_GNU_addr_base", sizeof("DW_AT_GNU_addr_base"));
            break;
        case DW_AT_GNU_pubnames:
            memcpy(s, "DW_AT_GNU_pubnames", sizeof("DW_AT_GNU_pubnames"));
            break;
        case DW_AT_GNU_pubtypes:
            memcpy(s, "DW_AT_GNU_pubtypes", sizeof("DW_AT_GNU_pubtypes"));
            break;
        case DW_AT_GNU_discriminator:
            memcpy(s, "DW_AT_GNU_discriminator",
                   sizeof("DW_AT_GNU_discriminator"));
            break;
        case DW_AT_BORLAND_property_read:
            memcpy(s, "DW_AT_BORLAND_property_read",
                   sizeof("DW_AT_BORLAND_property_read"));
            break;
        case DW_AT_BORLAND_property_write:
            memcpy(s, "DW_AT_BORLAND_property_write",
                   sizeof("DW_AT_BORLAND_property_write"));
            break;
        case DW_AT_BORLAND_property_implements:
            memcpy(s, "DW_AT_BORLAND_property_implements",
                   sizeof("DW_AT_BORLAND_property_implements"));
            break;
        case DW_AT_BORLAND_property_index:
            memcpy(s, "DW_AT_BORLAND_property_index",
                   sizeof("DW_AT_BORLAND_property_index"));
            break;
        case DW_AT_BORLAND_property_default:
            memcpy(s, "DW_AT_BORLAND_property_default",
                   sizeof("DW_AT_BORLAND_property_default"));
            break;
        case DW_AT_BORLAND_Delphi_unit:
            memcpy(s, "DW_AT_BORLAND_Delphi_unit",
                   sizeof("DW_AT_BORLAND_Delphi_unit"));
            break;
        case DW_AT_BORLAND_Delphi_class:
            memcpy(s, "DW_AT_BORLAND_Delphi_class",
                   sizeof("DW_AT_BORLAND_Delphi_class"));
            break;
        case DW_AT_BORLAND_Delphi_record:
            memcpy(s, "DW_AT_BORLAND_Delphi_record",
                   sizeof("DW_AT_BORLAND_Delphi_record"));
            break;
        case DW_AT_BORLAND_Delphi_metaclass:
            memcpy(s, "DW_AT_BORLAND_Delphi_metaclass",
                   sizeof("DW_AT_BORLAND_Delphi_metaclass"));
            break;
        case DW_AT_BORLAND_Delphi_constructor:
            memcpy(s, "DW_AT_BORLAND_Delphi_constructor",
                   sizeof("DW_AT_BORLAND_Delphi_constructor"));
            break;
        case DW_AT_BORLAND_Delphi_destructor:
            memcpy(s, "DW_AT_BORLAND_Delphi_destructor",
                   sizeof("DW_AT_BORLAND_Delphi_destructor"));
            break;
        case DW_AT_BORLAND_Delphi_anonymous_method:
            memcpy(s, "DW_AT_BORLAND_Delphi_anonymous_method",
                   sizeof("DW_AT_BORLAND_Delphi_anonymous_method"));
            break;
        case DW_AT_BORLAND_Delphi_interface:
            memcpy(s, "DW_AT_BORLAND_Delphi_interface",
                   sizeof("DW_AT_BORLAND_Delphi_interface"));
            break;
        case DW_AT_BORLAND_Delphi_ABI:
            memcpy(s, "DW_AT_BORLAND_Delphi_ABI",
                   sizeof("DW_AT_BORLAND_Delphi_ABI"));
            break;
        case DW_AT_BORLAND_Delphi_return:
            memcpy(s, "DW_AT_BORLAND_Delphi_return",
                   sizeof("DW_AT_BORLAND_Delphi_return"));
            break;
        case DW_AT_BORLAND_Delphi_frameptr:
            memcpy(s, "DW_AT_BORLAND_Delphi_frameptr",
                   sizeof("DW_AT_BORLAND_Delphi_frameptr"));
            break;
        case DW_AT_BORLAND_closure:
            memcpy(s, "DW_AT_BORLAND_closure", sizeof("DW_AT_BORLAND_closure"));
            break;
        case DW_AT_LLVM_include_path:
            memcpy(s, "DW_AT_LLVM_include_path",
                   sizeof("DW_AT_LLVM_include_path"));
            break;
        case DW_AT_LLVM_config_macros:
            memcpy(s, "DW_AT_LLVM_config_macros",
                   sizeof("DW_AT_LLVM_config_macros"));
            break;
        case DW_AT_LLVM_sysroot:
            memcpy(s, "DW_AT_LLVM_sysroot", sizeof("DW_AT_LLVM_sysroot"));
            break;
        case DW_AT_LLVM_tag_offset:
            memcpy(s, "DW_AT_LLVM_tag_offset", sizeof("DW_AT_LLVM_tag_offset"));
            break;
        case DW_AT_LLVM_apinotes:
            memcpy(s, "DW_AT_LLVM_apinotes", sizeof("DW_AT_LLVM_apinotes"));
            break;
        case DW_AT_APPLE_optimized:
            memcpy(s, "DW_AT_APPLE_optimized", sizeof("DW_AT_APPLE_optimized"));
            break;
        case DW_AT_APPLE_flags:
            memcpy(s, "DW_AT_APPLE_flags", sizeof("DW_AT_APPLE_flags"));
            break;
        case DW_AT_APPLE_isa:
            memcpy(s, "DW_AT_APPLE_isa", sizeof("DW_AT_APPLE_isa"));
            break;
        case DW_AT_APPLE_block:
            memcpy(s, "DW_AT_APPLE_block", sizeof("DW_AT_APPLE_block"));
            break;
        case DW_AT_APPLE_major_runtime_vers:
            memcpy(s, "DW_AT_APPLE_major_runtime_vers",
                   sizeof("DW_AT_APPLE_major_runtime_vers"));
            break;
        case DW_AT_APPLE_runtime_class:
            memcpy(s, "DW_AT_APPLE_runtime_class",
                   sizeof("DW_AT_APPLE_runtime_class"));
            break;
        case DW_AT_APPLE_omit_frame_ptr:
            memcpy(s, "DW_AT_APPLE_omit_frame_ptr",
                   sizeof("DW_AT_APPLE_omit_frame_ptr"));
            break;
        case DW_AT_APPLE_property_name:
            memcpy(s, "DW_AT_APPLE_property_name",
                   sizeof("DW_AT_APPLE_property_name"));
            break;
        case DW_AT_APPLE_property_getter:
            memcpy(s, "DW_AT_APPLE_property_getter",
                   sizeof("DW_AT_APPLE_property_getter"));
            break;
        case DW_AT_APPLE_property_setter:
            memcpy(s, "DW_AT_APPLE_property_setter",
                   sizeof("DW_AT_APPLE_property_setter"));
            break;
        case DW_AT_APPLE_property_attribute:
            memcpy(s, "DW_AT_APPLE_property_attribute",
                   sizeof("DW_AT_APPLE_property_attribute"));
            break;
        case DW_AT_APPLE_objc_complete_type:
            memcpy(s, "DW_AT_APPLE_objc_complete_type",
                   sizeof("DW_AT_APPLE_objc_complete_type"));
            break;
        case DW_AT_APPLE_property:
            memcpy(s, "DW_AT_APPLE_property", sizeof("DW_AT_APPLE_property"));
            break;
        case DW_AT_APPLE_objc_direct:
            memcpy(s, "DW_AT_APPLE_objc_direct",
                   sizeof("DW_AT_APPLE_objc_direct"));
            break;
        case DW_AT_APPLE_sdk:
            memcpy(s, "DW_AT_APPLE_sdk", sizeof("DW_AT_APPLE_sdk"));
            break;
        default:
            printf("attr=%#x\n", attr);
            assert(0 && "UNREACHABLE");
    }
}

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

const char dw_form_str[][30] = {
    [DW_FORM_addr] = "DW_FORM_addr",
    [DW_FORM_block2] = "DW_FORM_block2",
    [DW_FORM_block4] = "DW_FORM_block4",
    [DW_FORM_data2] = "DW_FORM_data2",
    [DW_FORM_data4] = "DW_FORM_data4",
    [DW_FORM_data8] = "DW_FORM_data8",
    [DW_FORM_string] = "DW_FORM_string",
    [DW_FORM_block] = "DW_FORM_block",
    [DW_FORM_block1] = "DW_FORM_block1",
    [DW_FORM_data1] = "DW_FORM_data1",
    [DW_FORM_flag] = "DW_FORM_flag",
    [DW_FORM_sdata] = "DW_FORM_sdata",
    [DW_FORM_strp] = "DW_FORM_strp",
    [DW_FORM_udata] = "DW_FORM_udata",
    [DW_FORM_ref_addr] = "DW_FORM_ref_addr",
    [DW_FORM_ref1] = "DW_FORM_ref1",
    [DW_FORM_ref2] = "DW_FORM_ref2",
    [DW_FORM_ref4] = "DW_FORM_ref4",
    [DW_FORM_ref8] = "DW_FORM_ref8",
    [DW_FORM_ref_udata] = "DW_FORM_ref_udata",
    [DW_FORM_indirect] = "DW_FORM_indirect",
    [DW_FORM_sec_offset] = "DW_FORM_sec_offset",
    [DW_FORM_exprloc] = "DW_FORM_exprloc",
    [DW_FORM_flag_present] = "DW_FORM_flag_present",
    [DW_FORM_ref_sig8] = "DW_FORM_ref_sig8",
};

typedef enum : uint8_t {
    DW_LNE_end_sequence = 1,
    DW_LNE_set_address,
    DW_LNE_define_file,
    DW_LNE_set_discriminator,
} DW_LNE;

typedef enum : uint16_t {
    DW_LANG_C89 = 0x0001,
    DW_LANG_C = 0x0002,
    DW_LANG_Ada83 = 0x0003,
    DW_LANG_C_plus_plus = 0x0004,
    DW_LANG_Cobol74 = 0x0005,
    DW_LANG_Cobol85 = 0x0006,
    DW_LANG_Fortran77 = 0x0007,
    DW_LANG_Fortran90 = 0x0008,
    DW_LANG_Pascal83 = 0x0009,
    DW_LANG_Modula2 = 0x000a,
    DW_LANG_Java = 0x000b,
    DW_LANG_C99 = 0x000c,
    DW_LANG_Ada95 = 0x000d,
    DW_LANG_Fortran95 = 0x000e,
    DW_LANG_PLI = 0x000f,
    DW_LANG_ObjC = 0x0010,
    DW_LANG_ObjC_plus_plus = 0x0011,
    DW_LANG_UPC = 0x0012,
    DW_LANG_D = 0x0013,
    DW_LANG_Python = 0x0014,
    DW_LANG_OpenCL = 0x0015,
    DW_LANG_Go = 0x0016,
    DW_LANG_Modula3 = 0x0017,
    DW_LANG_Haskell = 0x0018,
    DW_LANG_C_plus_plus_03 = 0x0019,
    DW_LANG_C_plus_plus_11 = 0x001a,
    DW_LANG_OCaml = 0x001b,
    DW_LANG_Rust = 0x001c,
    DW_LANG_C11 = 0x001d,
    DW_LANG_Swift = 0x001e,
    DW_LANG_Julia = 0x001f,
    DW_LANG_Dylan = 0x0020,
    DW_LANG_C_plus_plus_14 = 0x0021,
    DW_LANG_Fortran03 = 0x0022,
    DW_LANG_Fortran08 = 0x0023,
    DW_LANG_RenderScript = 0x0024,
    DW_LANG_BLISS = 0x0025,
    DW_LANG_Mips_Assembler = 0x8001,
    DW_LANG_GOOGLE_RenderScript = 0x8e57,
    DW_LANG_BORLAND_Delphi = 0xb000,
} dw_lang;

typedef struct {
} dwarf_info;

typedef struct {
    dw_attribute attr;
    dw_form form;
} dw_attr_form;

typedef struct {
    u8 type;
    u8 tag;
    // bool has_children; ?
    gbArray(dw_attr_form) attr_forms;
} dw_abbrev_entry;

typedef struct {
    gbArray(dw_abbrev_entry) entries;
} dw_abbrev;

typedef struct {
    char* s;
    u32 offset;
} dw_string;

typedef struct {
    u64 low_pc;
    char* fn_name;
    char* directory;
    char* file;
    u16 high_pc;
    u16 line;
} dw_fn_decl;

typedef struct {
    u64 pc;
    u16 line;
} dw_line_entry;

typedef struct {
    u64 address;
    u16 line;
    // TODO: track column?
    int file;
    bool is_stmt;
} dw_line_section_fsm;

typedef struct {
    char* directory;
    char* file;
    char* fn_name;
    u16 line;
} stacktrace_entry;

typedef struct {
    gbArray(dw_fn_decl) fn_decls;
    gbArray(dw_line_entry) line_entries;
    gbArray(dw_string) debug_str_strings;
    gbArray(char*) debug_line_files;
} debug_data;

static void read_dwarf_ext_op(void* data, isize size, u64* offset,
                              dw_line_section_fsm* fsm,
                              gbArray(dw_line_entry) * line_entries) {
    assert(data != NULL);
    assert(offset != NULL);
    assert(fsm != NULL);
    assert(line_entries != NULL);

    const u64 start_offset = *offset;
    const DW_LNE* extended_opcode = &data[*offset];
    *offset += 1;
    printf("DW_EXT_OP=%d\n", *extended_opcode);

    switch (*extended_opcode) {
        case DW_LNE_end_sequence: {
            puts("DW_LNE_end_sequence");

            *fsm = (dw_line_section_fsm){0};
            break;
        }
        case DW_LNE_set_address: {
            const u64* a = &data[*offset];
            *offset += sizeof(u64);
            printf("DW_LNE_set_address addr=%#llx\n", *a);
            fsm->address = *a;

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

static void read_dwarf_section_debug_abbrev(gbAllocator allocator, void* data,
                                            const struct section_64* sec,
                                            dw_abbrev* abbrev) {
    assert(data != NULL);
    assert(sec != NULL);
    assert(abbrev != NULL);
    u64 offset = sec->offset;
    gb_array_init_reserve(abbrev->entries, allocator, 100);

    while (offset < sec->offset + sec->size) {
        dw_abbrev_entry entry = {0};
        entry.type = *(u8*)&data[offset++];
        if (entry.type == 0) break;

        entry.tag = *(u8*)&data[offset++];
        bool* has_children = &data[offset++];
        printf(".debug_abbrev: type_num=%d tag=%#x %s has_children=%d\n",
               entry.type, entry.tag, dw_tag_str[entry.tag], *has_children);

        gb_array_init_reserve(entry.attr_forms, allocator, 20);
        while (offset < sec->offset + sec->size) {
            dw_attr_form attr_form = {0};
            attr_form.attr = read_leb128_u64(data, &offset);
            attr_form.form = read_leb128_u64(data, &offset);
            if (attr_form.attr == 0 && attr_form.form == 0) break;

            char attr_str[50] = "";
            dw_attribute_to_str(attr_form.attr, attr_str, sizeof(attr_str));
            printf(".debug_abbrev: attr=%#x %s form=%#x %s\n", attr_form.attr,
                   attr_str, attr_form.form, dw_form_str[attr_form.form]);

            gb_array_append(entry.attr_forms, attr_form);
        }
        gb_array_append(abbrev->entries, entry);
    }
}

static void read_dwarf_section_debug_info(gbAllocator allocator, void* data,
                                          const struct section_64* sec,
                                          const dw_abbrev* abbrev,
                                          debug_data* dd) {
    assert(data != NULL);
    assert(sec != NULL);
    assert(abbrev != NULL);
    assert(dd != NULL);

    u64 offset = sec->offset;

    gb_array_init_reserve(dd->fn_decls, allocator, 100);

    // TODO: multiple compile units (CU)
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

    char* directory = NULL;
    while (offset < sec->offset + sec->size) {
        u8 type = *(u8*)&data[offset++];
        assert(type <= gb_array_count(abbrev->entries));
        if (type == 0) {
            continue;  // skip
        }

        const dw_abbrev_entry* entry = NULL;
        // TODO: pre-sort the entries to avoid the linear look-up each time?
        for (int i = 0; i < gb_array_count(abbrev->entries); i++) {
            if (abbrev->entries[i].type == type) {
                entry = &abbrev->entries[i];
                break;
            }
        }
        assert(entry != NULL);
        assert(entry->type == type);
        printf(".debug_info type=%#x tag=%#x %s\n", type, entry->tag,
               dw_tag_str[entry->tag]);

        dw_fn_decl* se = NULL;
        if (entry->tag == DW_TAG_subprogram) {
            gb_array_append(dd->fn_decls,
                            ((dw_fn_decl){.directory = directory}));
            const isize se_count = gb_array_count(dd->fn_decls);
            se = &(dd->fn_decls)[se_count - 1];
        }
        if (entry->tag == DW_TAG_compile_unit) {
        }

        for (int i = 0; i < gb_array_count(entry->attr_forms); i++) {
            const dw_attr_form af = entry->attr_forms[i];
            char attr_str[50] = "";
            dw_attribute_to_str(af.attr, attr_str, sizeof(attr_str));
            printf(".debug_info: attr=%#x %s form=%#x %s\n", af.attr, attr_str,
                   af.form, dw_form_str[af.form]);

            switch (af.form) {
                case DW_FORM_strp: {
                    u32 str_offset = *(u32*)&data[offset];
                    offset += 4;
                    printf("DW_FORM_strp: %#x ", str_offset);

                    char* s = NULL;
                    for (int i = 0; i < gb_array_count(dd->debug_str_strings);
                         i++) {
                        if (dd->debug_str_strings[i].offset == str_offset) {
                            s = dd->debug_str_strings[i].s;
                            break;
                        }
                    }
                    assert(s != NULL);
                    puts(s);
                    if (af.attr == DW_AT_name && se != NULL) {
                        se->fn_name = s;
                    }
                    if (af.attr == DW_AT_comp_dir) {
                        directory = s;
                    }
                    break;
                }
                case DW_FORM_data1: {
                    u8 val = *(u8*)&data[offset];
                    offset += 1;
                    printf("DW_FORM_data1: %#x\n", val);
                    if (af.attr == DW_AT_decl_file && se != NULL &&
                        se->file == NULL) {
                        assert(dd->debug_line_files != NULL);
                        assert(val <= gb_array_count(dd->debug_line_files));
                        se->file = dd->debug_line_files[val - 1];
                    }
                    break;
                }
                case DW_FORM_data2: {
                    u16 lang = *(u16*)&data[offset];
                    offset += 2;
                    printf("DW_FORM_data2: %#x\n", lang);
                    assert(lang == DW_LANG_C99);
                    break;
                }
                case DW_FORM_data4: {
                    u32 val = *(u32*)&data[offset];
                    offset += 4;
                    printf("DW_FORM_data4: %#x\n", val);
                    if (af.attr == DW_AT_high_pc && se != NULL) {
                        se->high_pc = val;
                    }
                    break;
                }
                case DW_FORM_data8: {
                    u64 val = *(u64*)&data[offset];
                    offset += 8;
                    printf("DW_FORM_data8: %#llx\n", val);
                    break;
                }
                case DW_FORM_ref1: {
                    u8 val = *(u8*)&data[offset];
                    offset += 1;
                    printf("DW_FORM_ref1: %#x\n", val);
                    break;
                }
                case DW_FORM_ref2: {
                    u16 val = *(u16*)&data[offset];
                    offset += 2;
                    printf("DW_FORM_ref2: %#x\n", val);
                    break;
                }
                case DW_FORM_ref4: {
                    u32 val = *(u32*)&data[offset];
                    offset += 4;
                    printf("DW_FORM_ref4: %#x\n", val);
                    break;
                }
                case DW_FORM_ref8: {
                    u64 val = *(u64*)&data[offset];
                    offset += 8;
                    printf("DW_FORM_ref8: %#llx\n", val);
                    break;
                }
                case DW_FORM_sec_offset: {
                    u32 val = *(u32*)&data[offset];
                    offset += 4;
                    printf("DW_FORM_sec_offset: %#x\n", val);
                    break;
                }
                case DW_FORM_addr: {
                    u64 addr = *(u64*)&data[offset];
                    offset += 8;
                    printf("DW_FORM_addr: %#llx\n", addr);

                    if (af.attr == DW_AT_low_pc && se != NULL) {
                        se->low_pc = addr;
                    }
                    break;
                }
                case DW_FORM_flag_present: {
                    puts("DW_FORM_flag_present (true)");
                    break;
                }
                case DW_FORM_exprloc: {
                    const u64 length = read_leb128_u64(data, &offset);
                    offset += length;
                    printf("DW_FORM_exprloc: length=%lld\n", length);

                    break;
                }
                default:
                    assert(0 && "UNIMPLEMENTED");
            }
        }
    }
}

void read_dwarf_section_debug_str(gbAllocator allocator, void* data,
                                  const struct section_64* sec,
                                  debug_data* dd) {
    assert(data != NULL);
    assert(sec != NULL);
    assert(dd != NULL);

    u64 offset = sec->offset;
    u64 i = 0;

    gb_array_init_reserve(dd->debug_str_strings, allocator, 100);
    while (offset < sec->offset + sec->size) {
        char* s = &data[offset];
        if (*s == 0) {
            offset++;
            continue;
        }

        char* end = memchr(&data[offset], 0, sec->offset + sec->size);
        assert(end != NULL);
        printf("- [%llu] %s\n", i, s);
        dw_string str = {.s = s, .offset = offset - sec->offset};
        gb_array_append(dd->debug_str_strings, str);
        offset += end - s;
        i++;
    }
}

static void read_dwarf_section_debug_line(gbAllocator allocator, void* data,
                                          const struct section_64* sec,
                                          debug_data* dd) {
    assert(data != NULL);
    assert(sec != NULL);
    assert(dd != NULL);
    gb_array_init_reserve(dd->line_entries, allocator, 300);

    u64 offset = sec->offset;
    const dwarf_debug_line_header* ddlh = &data[offset];
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
        if (end - s == 0) {
            offset += 1;
            continue;
        }
        printf("- %s (%ld)\n", s, end - s);

        offset += end - s;
        if (*(end + 1) == 0) {
            offset += 2;
            break;
        }
    }
    puts("Files:");
    gb_array_init_reserve(dd->debug_line_files, allocator, 10);
    while (offset < sec->offset + sec->size) {
        char* s = &data[offset];
        if (*s == 0) {
            offset += 1;
            break;
        }
        char* end = memchr(&data[offset], 0, sec->offset + sec->size);
        assert(end != NULL);

        offset += end - s + 1;
        const u64 dir_index = read_leb128_u64(data, &offset);
        const u64 modtime = read_leb128_u64(data, &offset);

        const u64 length = read_leb128_u64(data, &offset);

        gb_array_append(dd->debug_line_files, s);
        printf(
            "- %s dir_index=%llu modtime=%llu "
            "length=%llu\n",
            s, dir_index, modtime, length);
    }
    puts("");

    dw_line_section_fsm fsm = {0};

    while (offset < sec->offset + sec->size) {
        DW_LNS* opcode = &data[offset];
        offset += 1;
        printf("DW_OP=%#x offset=%#llx address=%#llx line=%d\n", *opcode,
               offset, fsm.address, fsm.line + 1);
        switch (*opcode) {
            case DW_LNS_extended_op: {
                const u64 size = read_leb128_u64(data, &offset);
                printf("DW_LNS_extended_op size=%#llx\n", size);

                read_dwarf_ext_op(data, size, &offset, &fsm, &dd->line_entries);
                break;
            }
            case DW_LNS_copy:
                puts("DW_LNS_copy");
                break;
            case DW_LNS_advance_pc: {
                const u64 decoded = read_leb128_u64(data, &offset);
                printf("DW_LNS_advance_pc leb128=%#llx\n", decoded);
                fsm.address += decoded;
                break;
            }
            case DW_LNS_advance_line: {
                const u64 l = read_leb128_s64(data, &offset);
                printf("DW_LNS_advance_line line=%lld\n", l);
                fsm.line = l;
                dw_line_entry e = {.pc = fsm.address, .line = fsm.line};
                gb_array_append(dd->line_entries, e);
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
                fsm.is_stmt = !fsm.is_stmt;
                break;
            case DW_LNS_set_basic_block:
                break;
            case DW_LNS_const_add_pc: {
                const u8 op = 255 - ddlh->opcode_base;
                fsm.address +=
                    op / ddlh->line_range * ddlh->min_instruction_length;
                // TODO: op_index
                printf("address+=%#x -> address=%#llx\n",
                       op / ddlh->line_range * ddlh->min_instruction_length,
                       fsm.address);
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
                fsm.address +=
                    op / ddlh->line_range * ddlh->min_instruction_length;

                const u16 old_line = fsm.line;
                fsm.line += ddlh->line_base + op % ddlh->line_range;
                if (fsm.line != old_line) {
                    dw_line_entry e = {.pc = fsm.address, .line = fsm.line};
                    gb_array_append(dd->line_entries, e);
                }
                printf("address+=%d line+=%d\n",
                       op / ddlh->line_range * ddlh->min_instruction_length,
                       ddlh->line_base + op % ddlh->line_range);
            }
        }
    }
}

void stacktrace_find_entry(const debug_data* dd, u64 pc, stacktrace_entry* se) {
    for (int i = 0; i < gb_array_count(dd->fn_decls); i++) {
        const dw_fn_decl* fd = &dd->fn_decls[i];
        if (fd->low_pc <= pc && pc <= fd->low_pc + fd->high_pc) {
            se->directory = fd->directory;
            se->file = fd->file;
            se->fn_name = fd->fn_name;
            break;
        }
    }
    if (se->directory == NULL) return;
    assert(se->file != NULL);
    assert(se->fn_name != NULL);

    const dw_line_entry* le = NULL;
    for (int i = 0; i < gb_array_count(dd->line_entries); i++) {
        if (le != NULL && le->pc > pc) {
            se->line = le->line;
            break;
        }
        le = &dd->line_entries[i];
    }
}

static void read_macho_dsym(gbAllocator allocator, void* data, isize size,
                            debug_data* dd) {
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

    // Remember where those sections were since they might be
    // out-of-order and we need to first read the abbrev section & str section,
    // and then the info section.
    const struct section_64* sec_abbrev = NULL;
    const struct section_64* sec_info = NULL;
    const struct section_64* sec_str = NULL;
    const struct section_64* sec_line = NULL;

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
                    "LC_UUID uuid=%#x %#x %#x %#x %#x %#x %#x %#x %#x %#x "
                    "%#x "
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
                        "SECTION sectname=%s segname=%s addr=%#llx "
                        "size=%#llx "
                        "offset=%#x align=%#x reloff=%#x nreloc=%d "
                        "flags=%#x\n",
                        sec->sectname, sec->segname, sec->addr, sec->size,
                        sec->offset, sec->align, sec->reloff, sec->nreloc,
                        sec->flags);

                    if (strcmp(sec->sectname, "__debug_line") == 0) {
                        sec_line = sec;
                    } else if (strcmp(sec->sectname, "__debug_str") == 0) {
                        sec_str = sec;
                    } else if (strcmp(sec->sectname, "__debug_info") == 0) {
                        sec_info = sec;
                    } else if (strcmp(sec->sectname, "__debug_abbrev") == 0) {
                        sec_abbrev = sec;
                    }
                }

                break;
            }
            default:
                assert(0 && "UNIMPLEMENTED - catch all");
        }
    }
    assert(sec_abbrev != NULL);
    assert(sec_info != NULL);
    assert(sec_str != NULL);
    assert(sec_line != NULL);

    read_dwarf_section_debug_str(allocator, data, sec_str, dd);
    dw_abbrev abbrev = {0};
    read_dwarf_section_debug_line(allocator, data, sec_line, dd);
    read_dwarf_section_debug_abbrev(allocator, data, sec_abbrev, &abbrev);

    read_dwarf_section_debug_info(allocator, data, sec_info, &abbrev, dd);

    for (int i = 0; i < gb_array_count(dd->fn_decls); i++) {
        dw_fn_decl* fd = &(dd->fn_decls)[i];
        printf(
            "dw_fn_decl: low_pc=%#llx high_pc=%#hx fn_name=%s "
            "file=%s/%s\n",
            fd->low_pc, fd->high_pc, fd->fn_name, fd->directory, fd->file);
    }

    for (int i = 0; i < gb_array_count(dd->line_entries); i++) {
        dw_line_entry* le = &(dd->line_entries)[i];
        printf("dw_line_entry: line=%d pc=%#llx\n", 1 + le->line, le->pc);
    }
}

debug_data dd = {0};

void stacktrace_print() {
    if (dd.debug_str_strings == NULL) {
        gbAllocator allocator = gb_heap_allocator();
        const char path[100] = "./test.dSYM/Contents/Resources/DWARF/test";
        gbFileContents contents = gb_file_read_contents(allocator, true, path);
        read_macho_dsym(allocator, contents.data, contents.size, &dd);
    }

    uintptr_t* rbp = __builtin_frame_address(0);
    while (rbp != 0 && *rbp != 0) {
        uintptr_t rip = *(rbp + 1);
        uintptr_t rsp = *(rbp + 2);
        printf("rbp=%p rip=%#lx rsp=%#lx __builtin_frame_address=%p\n", rbp,
               rip, rsp, __builtin_frame_address(0));
        rbp = (uintptr_t*)*rbp;

        stacktrace_entry se = {0};
        stacktrace_find_entry(&dd, rip, &se);
        if (se.directory != NULL) {
            printf("stacktrace_entry: %s/%s:%s:%d\n", se.directory, se.file,
                   se.fn_name, se.line);
        }
    }
}
