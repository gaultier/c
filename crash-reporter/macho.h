#pragma once

#include <assert.h>
#include <libproc.h>
#include <mach-o/loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <unistd.h>

#include "../pg/pg.h"

static pg_logger_t logger = {.level = PG_LOG_INFO};

static void read_data(uint8_t *data, uint64_t data_size, uint64_t *offset,
                      void *res, uint64_t res_size) {
  assert(data != NULL);
  assert(offset != NULL);
  assert(res != NULL);
  assert(*offset + res_size < data_size);
  memcpy(res, &data[*offset], res_size);
  *offset += res_size;
}

static uint64_t read_leb128_u64(uint8_t *data, uint64_t size,
                                uint64_t *offset) {
  uint64_t result = 0;
  uint64_t shift = 0;
  while (true) {
    uint8_t byte = 0;
    read_data(data, size, offset, &byte, sizeof(byte));
    result |= (byte & 0x7fU) << shift;
    if ((byte & 0x80) == 0)
      break;
    shift += 7;
  }
  return result;
}

// TODO: check overflow
static int64_t read_leb128_i64(uint8_t *data, uint64_t size, uint64_t *offset) {
  int64_t result = 0;
  uint64_t shift = 0;
  const uint64_t bit_count = sizeof(uint64_t) * 8;
  uint8_t byte = 0;
  do {
    read_data(data, size, offset, &byte, sizeof(byte));
    result |= (byte & 0x7f) << shift;
    shift += 7;
  } while (((byte & 0x80) != 0));

  /* sign bit of byte is second high-order bit (0x40) */
  if (shift < bit_count && ((byte & 0x40) != 0)) {
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
} dwarf_debug_line_header_t;

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
} dw_lns_t;

typedef enum {
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
} dw_tag_t;

typedef enum : uint16_t {
  DW_AT_none = 0,
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
  DW_AT_string_length_bit_size = 0x6f,
  DW_AT_string_length_byte_size = 0x70,
  DW_AT_rank = 0x71,
  DW_AT_str_offsets_base = 0x72,
  DW_AT_addr_base = 0x73,
  DW_AT_rnglists_base = 0x74,
  DW_AT_dwo_id = 0x75,
  DW_AT_dwo_name = 0x76,
  DW_AT_reference = 0x77,
  DW_AT_rvalue_reference = 0x78,
  DW_AT_macros = 0x79,
  DW_AT_call_all_calls = 0x7a,
  DW_AT_call_all_source_calls = 0x7b,
  DW_AT_call_all_tail_calls = 0x7c,
  DW_AT_call_return_pc = 0x7d,
  DW_AT_call_value = 0x7e,
  DW_AT_call_origin = 0x7f,
  DW_AT_call_parameter = 0x80,
  DW_AT_call_pc = 0x81,
  DW_AT_call_tail_call = 0x82,
  DW_AT_call_target = 0x83,
  DW_AT_call_target_clobbered = 0x84,
  DW_AT_call_data_location = 0x85,
  DW_AT_call_data_value = 0x86,
  DW_AT_noreturn = 0x87,
  DW_AT_alignment = 0x88,
  DW_AT_export_symbols = 0x89,
  DW_AT_deleted = 0x8a,
  DW_AT_defaulted = 0x8b,
  DW_AT_loclists_base = 0x8c,
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
} dw_attribute_t;

typedef enum : uint8_t {
  DW_FORM_none = 0,
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
} dw_form_t;

typedef enum : uint8_t {
  DW_LNE_none = 0,
  DW_LNE_end_sequence = 1,
  DW_LNE_set_address,
  DW_LNE_define_file,
  DW_LNE_set_discriminator,
} dw_lne_t;

typedef struct {
  dw_attribute_t attr;
  dw_form_t form;
  PG_PAD(1);
} dw_attr_form_t;

typedef struct {
  pg_array_t(dw_attr_form_t) attr_forms;
  uint8_t type;
  uint8_t tag;
  PG_PAD(6);
} dw_abbrev_entry_t;

typedef struct {
  pg_array_t(dw_abbrev_entry_t) entries;
} dw_abbrev_t;

typedef struct {
  char *s;
  uint32_t offset;
  PG_PAD(4);
} dw_string_t;

typedef struct {
  uint64_t low_pc;
  char *fn_name;
  char *directory;
  char *file;
  uint16_t high_pc;
  uint16_t line;
  PG_PAD(4);
} dw_fn_decl_t;

typedef struct {
  uint64_t pc;
  uint64_t file;
  uint16_t line;
  PG_PAD(6);
} dw_line_entry_t;

typedef struct {
  uint64_t address;
  uint64_t file;
  uint16_t line;
  bool is_stmt;
  PG_PAD(5);
  // TODO: track column?
} dw_line_section_fsm_t;

typedef struct {
  char *directory;
  char *file;
  char *fn_name;
  uint64_t line;
} stacktrace_entry_t;

typedef struct {
  pg_array_t(uint8_t) contents;
  char *directory;
  char *file;
  pg_array_t(uint16_t) newline_offsets;
} source_file_t;

typedef struct {
  uint64_t pie_displacement;
  pg_array_t(dw_fn_decl_t) fn_decls;
  pg_array_t(dw_line_entry_t) line_entries;
  pg_array_t(dw_string_t) debug_str_strings;
  pg_array_t(char *) debug_line_files;
  pg_array_t(source_file_t) sources;
} debug_data_t;

static const char dw_tag_str[][40] = {
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

static char *dw_attribute_to_str(dw_attribute_t attr) {
  switch (attr) {
  case DW_AT_sibling:
    return "DW_AT_sibling";
  case DW_AT_location:
    return "DW_AT_location";
  case DW_AT_name:
    return "DW_AT_name";
  case DW_AT_ordering:
    return "DW_AT_ordering";
  case DW_AT_byte_size:
    return "DW_AT_byte_size";
  case DW_AT_bit_offset:
    return "DW_AT_bit_offset";
  case DW_AT_bit_size:
    return "DW_AT_bit_size";
  case DW_AT_stmt_list:
    return "DW_AT_stmt_list";
  case DW_AT_low_pc:
    return "DW_AT_low_pc";
  case DW_AT_high_pc:
    return "DW_AT_high_pc";
  case DW_AT_language:
    return "DW_AT_language";
  case DW_AT_discr:
    return "DW_AT_discr";
  case DW_AT_discr_value:
    return "DW_AT_discr_value";
  case DW_AT_visibility:
    return "DW_AT_visibility";
  case DW_AT_import:
    return "DW_AT_import";
  case DW_AT_string_length:
    return "DW_AT_string_length";
  case DW_AT_common_reference:
    return "DW_AT_common_reference";
  case DW_AT_comp_dir:
    return "DW_AT_comp_dir";
  case DW_AT_const_value:
    return "DW_AT_const_value";
  case DW_AT_containing_type:
    return "DW_AT_containing_type";
  case DW_AT_default_value:
    return "DW_AT_default_value";
  case DW_AT_inline:
    return "DW_AT_inline";
  case DW_AT_is_optional:
    return "DW_AT_is_optional";
  case DW_AT_lower_bound:
    return "DW_AT_lower_bound";
  case DW_AT_producer:
    return "DW_AT_producer";
  case DW_AT_prototyped:
    return "DW_AT_prototyped";
  case DW_AT_return_addr:
    return "DW_AT_return_addr";
  case DW_AT_start_scope:
    return "DW_AT_start_scope";
  case DW_AT_bit_stride:
    return "DW_AT_bit_stride";
  case DW_AT_upper_bound:
    return "DW_AT_upper_bound";
  case DW_AT_abstract_origin:
    return "DW_AT_abstract_origin";
  case DW_AT_accessibility:
    return "DW_AT_accessibility";
  case DW_AT_address_class:
    return "DW_AT_address_class";
  case DW_AT_artificial:
    return "DW_AT_artificial";
  case DW_AT_base_types:
    return "DW_AT_base_types";
  case DW_AT_calling_convention:
    return "DW_AT_calling_convention";
  case DW_AT_count:
    return "DW_AT_count";
  case DW_AT_data_member_location:
    return "DW_AT_data_member_location";
  case DW_AT_decl_column:
    return "DW_AT_decl_column";
  case DW_AT_decl_file:
    return "DW_AT_decl_file";
  case DW_AT_decl_line:
    return "DW_AT_decl_line";
  case DW_AT_declaration:
    return "DW_AT_declaration";
  case DW_AT_discr_list:
    return "DW_AT_discr_list";
  case DW_AT_encoding:
    return "DW_AT_encoding";
  case DW_AT_external:
    return "DW_AT_external";
  case DW_AT_frame_base:
    return "DW_AT_frame_base";
  case DW_AT_friend:
    return "DW_AT_friend";
  case DW_AT_identifier_case:
    return "DW_AT_identifier_case";
  case DW_AT_macro_info:
    return "DW_AT_macro_info";
  case DW_AT_namelist_item:
    return "DW_AT_namelist_item";
  case DW_AT_priority:
    return "DW_AT_priority";
  case DW_AT_segment:
    return "DW_AT_segment";
  case DW_AT_specification:
    return "DW_AT_specification";
  case DW_AT_static_link:
    return "DW_AT_static_link";
  case DW_AT_type:
    return "DW_AT_type";
  case DW_AT_use_location:
    return "DW_AT_use_location";
  case DW_AT_variable_parameter:
    return "DW_AT_variable_parameter";
  case DW_AT_virtuality:
    return "DW_AT_virtuality";
  case DW_AT_vtable_elem_location:
    return "DW_AT_vtable_elem_location";
  case DW_AT_allocated:
    return "DW_AT_allocated";
  case DW_AT_associated:
    return "DW_AT_associated";
  case DW_AT_data_location:
    return "DW_AT_data_location";
  case DW_AT_byte_stride:
    return "DW_AT_byte_stride";
  case DW_AT_entry_pc:
    return "DW_AT_entry_pc";
  case DW_AT_use_UTF8:
    return "DW_AT_use_UTF8";
  case DW_AT_extension:
    return "DW_AT_extension";
  case DW_AT_ranges:
    return "DW_AT_ranges";
  case DW_AT_trampoline:
    return "DW_AT_trampoline";
  case DW_AT_call_column:
    return "DW_AT_call_column";
  case DW_AT_call_file:
    return "DW_AT_call_file";
  case DW_AT_call_line:
    return "DW_AT_call_line";
  case DW_AT_description:
    return "DW_AT_description";
  case DW_AT_binary_scale:
    return "DW_AT_binary_scale";
  case DW_AT_decimal_scale:
    return "DW_AT_decimal_scale";
  case DW_AT_small:
    return "DW_AT_small";
  case DW_AT_decimal_sign:
    return "DW_AT_decimal_sign";
  case DW_AT_digit_count:
    return "DW_AT_digit_count";
  case DW_AT_picture_string:
    return "DW_AT_picture_string";
  case DW_AT_mutable:
    return "DW_AT_mutable";
  case DW_AT_threads_scaled:
    return "DW_AT_threads_scaled";
  case DW_AT_explicit:
    return "DW_AT_explicit";
  case DW_AT_object_pointer:
    return "DW_AT_object_pointer";
  case DW_AT_endianity:
    return "DW_AT_endianity";
  case DW_AT_elemental:
    return "DW_AT_elemental";
  case DW_AT_pure:
    return "DW_AT_pure";
  case DW_AT_recursive:
    return "DW_AT_recursive";
  case DW_AT_signature:
    return "DW_AT_signature";
  case DW_AT_main_subprogram:
    return "DW_AT_main_subprogram";
  case DW_AT_data_bit_offset:
    return "DW_AT_data_bit_offset";
  case DW_AT_const_expr:
    return "DW_AT_const_expr";
  case DW_AT_enum_class:
    return "DW_AT_enum_class";
  case DW_AT_linkage_name:
    return "DW_AT_linkage_name";
  case DW_AT_string_length_bit_size:
    return "DW_AT_string_length_bit_size";
  case DW_AT_string_length_byte_size:
    return "DW_AT_string_length_byte_size";
  case DW_AT_rank:
    return "DW_AT_rank";
  case DW_AT_str_offsets_base:
    return "DW_AT_str_offsets_base";
  case DW_AT_addr_base:
    return "DW_AT_addr_base";
  case DW_AT_rnglists_base:
    return "DW_AT_rnglists_base";
  case DW_AT_dwo_id:
    return "DW_AT_dwo_id";
  case DW_AT_dwo_name:
    return "DW_AT_dwo_name";
  case DW_AT_reference:
    return "DW_AT_reference";
  case DW_AT_rvalue_reference:
    return "DW_AT_rvalue_reference";
  case DW_AT_macros:
    return "DW_AT_macros";
  case DW_AT_call_all_calls:
    return "DW_AT_call_all_calls";
  case DW_AT_call_all_source_calls:
    return "DW_AT_call_all_source_calls";
  case DW_AT_call_all_tail_calls:
    return "DW_AT_call_all_tail_calls";
  case DW_AT_call_return_pc:
    return "DW_AT_call_return_pc";
  case DW_AT_call_value:
    return "DW_AT_call_value";
  case DW_AT_call_origin:
    return "DW_AT_call_origin";
  case DW_AT_call_parameter:
    return "DW_AT_call_parameter";
  case DW_AT_call_pc:
    return "DW_AT_call_pc";
  case DW_AT_call_tail_call:
    return "DW_AT_call_tail_call";
  case DW_AT_call_target:
    return "DW_AT_call_target";
  case DW_AT_call_target_clobbered:
    return "DW_AT_call_target_clobbered";
  case DW_AT_call_data_location:
    return "DW_AT_call_data_location";
  case DW_AT_call_data_value:
    return "DW_AT_call_data_value";
  case DW_AT_noreturn:
    return "DW_AT_noreturn";
  case DW_AT_alignment:
    return "DW_AT_alignment";
  case DW_AT_export_symbols:
    return "DW_AT_export_symbols";
  case DW_AT_deleted:
    return "DW_AT_deleted";
  case DW_AT_defaulted:
    return "DW_AT_defaulted";
  case DW_AT_loclists_base:
    return "DW_AT_loclists_base";
  case DW_AT_MIPS_loop_begin:
    return "DW_AT_MIPS_loop_begin";
  case DW_AT_MIPS_tail_loop_begin:
    return "DW_AT_MIPS_tail_loop_begin";
  case DW_AT_MIPS_epilog_begin:
    return "DW_AT_MIPS_epilog_begin";
  case DW_AT_MIPS_loop_unroll_factor:
    return "DW_AT_MIPS_loop_unroll_factor";
  case DW_AT_MIPS_software_pipeline_depth:
    return "DW_AT_MIPS_software_pipeline_depth";
  case DW_AT_MIPS_linkage_name:
    return "DW_AT_MIPS_linkage_name";
  case DW_AT_MIPS_stride:
    return "DW_AT_MIPS_stride";
  case DW_AT_MIPS_abstract_name:
    return "DW_AT_MIPS_abstract_name";
  case DW_AT_MIPS_clone_origin:
    return "DW_AT_MIPS_clone_origin";
  case DW_AT_MIPS_has_inlines:
    return "DW_AT_MIPS_has_inlines";
  case DW_AT_MIPS_stride_byte:
    return "DW_AT_MIPS_stride_byte";
  case DW_AT_MIPS_stride_elem:
    return "DW_AT_MIPS_stride_elem";
  case DW_AT_MIPS_ptr_dopetype:
    return "DW_AT_MIPS_ptr_dopetype";
  case DW_AT_MIPS_allocatable_dopetype:
    return "DW_AT_MIPS_allocatable_dopetype";
  case DW_AT_MIPS_assumed_shape_dopetype:
    return "DW_AT_MIPS_assumed_shape_dopetype";
  case DW_AT_MIPS_assumed_size:
    return "DW_AT_MIPS_assumed_size";
  case DW_AT_sf_names:
    return "DW_AT_sf_names";
  case DW_AT_src_info:
    return "DW_AT_src_info";
  case DW_AT_mac_info:
    return "DW_AT_mac_info";
  case DW_AT_src_coords:
    return "DW_AT_src_coords";
  case DW_AT_body_begin:
    return "DW_AT_body_begin";
  case DW_AT_body_end:
    return "DW_AT_body_end";
  case DW_AT_GNU_vector:
    return "DW_AT_GNU_vector";
  case DW_AT_GNU_template_name:
    return "DW_AT_GNU_template_name";
  case DW_AT_GNU_odr_signature:
    return "DW_AT_GNU_odr_signature";
  case DW_AT_GNU_call_site_value:
    return "DW_AT_GNU_call_site_value";
  case DW_AT_GNU_call_site_data_value:
    return "DW_AT_GNU_call_site_data_value";
  case DW_AT_GNU_call_site_target:
    return "DW_AT_GNU_call_site_target";
  case DW_AT_GNU_call_site_target_clobbered:
    return "DW_AT_GNU_call_site_target_clobbered";
  case DW_AT_GNU_tail_call:
    return "DW_AT_GNU_tail_call";
  case DW_AT_GNU_all_tail_call_sites:
    return "DW_AT_GNU_all_tail_call_sites";
  case DW_AT_GNU_all_call_sites:
    return "DW_AT_GNU_all_call_sites";
  case DW_AT_GNU_all_source_call_sites:
    return "DW_AT_GNU_all_source_call_sites";
  case DW_AT_GNU_macros:
    return "DW_AT_GNU_macros";
  case DW_AT_GNU_dwo_name:
    return "DW_AT_GNU_dwo_name";
  case DW_AT_GNU_dwo_id:
    return "DW_AT_GNU_dwo_id";
  case DW_AT_GNU_ranges_base:
    return "DW_AT_GNU_ranges_base";
  case DW_AT_GNU_addr_base:
    return "DW_AT_GNU_addr_base";
  case DW_AT_GNU_pubnames:
    return "DW_AT_GNU_pubnames";
  case DW_AT_GNU_pubtypes:
    return "DW_AT_GNU_pubtypes";
  case DW_AT_GNU_discriminator:
    return "DW_AT_GNU_discriminator";
  case DW_AT_BORLAND_property_read:
    return "DW_AT_BORLAND_property_read";
  case DW_AT_BORLAND_property_write:
    return "DW_AT_BORLAND_property_write";
  case DW_AT_BORLAND_property_implements:
    return "DW_AT_BORLAND_property_implements";
  case DW_AT_BORLAND_property_index:
    return "DW_AT_BORLAND_property_index";
  case DW_AT_BORLAND_property_default:
    return "DW_AT_BORLAND_property_default";
  case DW_AT_BORLAND_Delphi_unit:
    return "DW_AT_BORLAND_Delphi_unit";
  case DW_AT_BORLAND_Delphi_class:
    return "DW_AT_BORLAND_Delphi_class";
  case DW_AT_BORLAND_Delphi_record:
    return "DW_AT_BORLAND_Delphi_record";
  case DW_AT_BORLAND_Delphi_metaclass:
    return "DW_AT_BORLAND_Delphi_metaclass";
  case DW_AT_BORLAND_Delphi_constructor:
    return "DW_AT_BORLAND_Delphi_constructor";
  case DW_AT_BORLAND_Delphi_destructor:
    return "DW_AT_BORLAND_Delphi_destructor";
  case DW_AT_BORLAND_Delphi_anonymous_method:
    return "DW_AT_BORLAND_Delphi_anonymous_method";
  case DW_AT_BORLAND_Delphi_interface:
    return "DW_AT_BORLAND_Delphi_interface";
  case DW_AT_BORLAND_Delphi_ABI:
    return "DW_AT_BORLAND_Delphi_ABI";
  case DW_AT_BORLAND_Delphi_return:
    return "DW_AT_BORLAND_Delphi_return";
  case DW_AT_BORLAND_Delphi_frameptr:
    return "DW_AT_BORLAND_Delphi_frameptr";
  case DW_AT_BORLAND_closure:
    return "DW_AT_BORLAND_closure";
  case DW_AT_LLVM_include_path:
    return "DW_AT_LLVM_include_path";
  case DW_AT_LLVM_config_macros:
    return "DW_AT_LLVM_config_macros";
  case DW_AT_LLVM_sysroot:
    return "DW_AT_LLVM_sysroot";
  case DW_AT_LLVM_tag_offset:
    return "DW_AT_LLVM_tag_offset";
  case DW_AT_LLVM_apinotes:
    return "DW_AT_LLVM_apinotes";
  case DW_AT_APPLE_optimized:
    return "DW_AT_APPLE_optimized";
  case DW_AT_APPLE_flags:
    return "DW_AT_APPLE_flags";
  case DW_AT_APPLE_isa:
    return "DW_AT_APPLE_isa";
  case DW_AT_APPLE_block:
    return "DW_AT_APPLE_block";
  case DW_AT_APPLE_major_runtime_vers:
    return "DW_AT_APPLE_major_runtime_vers";
  case DW_AT_APPLE_runtime_class:
    return "DW_AT_APPLE_runtime_class";
  case DW_AT_APPLE_omit_frame_ptr:
    return "DW_AT_APPLE_omit_frame_ptr";
  case DW_AT_APPLE_property_name:
    return "DW_AT_APPLE_property_name";
  case DW_AT_APPLE_property_getter:
    return "DW_AT_APPLE_property_getter";
  case DW_AT_APPLE_property_setter:
    return "DW_AT_APPLE_property_setter";
  case DW_AT_APPLE_property_attribute:
    return "DW_AT_APPLE_property_attribute";
  case DW_AT_APPLE_objc_complete_type:
    return "DW_AT_APPLE_objc_complete_type";
  case DW_AT_APPLE_property:
    return "DW_AT_APPLE_property";
  case DW_AT_APPLE_objc_direct:
    return "DW_AT_APPLE_objc_direct";
  case DW_AT_APPLE_sdk:
    return "DW_AT_APPLE_sdk";
  default:
    pg_log_debug(&logger, "attr=%#x\n", attr);
    assert(0 && "UNREACHABLE");
  }
}

static const char dw_form_str[][30] = {
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

static void read_dwarf_ext_op(uint8_t *data, uint64_t size, uint64_t *offset,
                              dw_line_section_fsm_t *fsm,
                              pg_array_t(dw_line_entry_t) * line_entries,
                              uint64_t ext_op_size) {
  assert(data != NULL);
  assert(offset != NULL);
  assert(fsm != NULL);
  assert(line_entries != NULL);

  const uint64_t start_offset = *offset;
  dw_lne_t extended_opcode = 0;
  read_data(data, size, offset, &extended_opcode, sizeof(extended_opcode));
  pg_log_debug(&logger, "DW_EXT_OP=%d\n", extended_opcode);

  switch (extended_opcode) {
  case DW_LNE_none:
    assert(0);
  case DW_LNE_end_sequence: {
    pg_log_debug(&logger, "DW_LNE_end_sequence");

    *fsm = (dw_line_section_fsm_t){.line = 1, .file = 1};
    break;
  }
  case DW_LNE_set_address: {
    uint64_t a = 0;
    read_data(data, size, offset, &a, sizeof(a));
    pg_log_debug(&logger, "DW_LNE_set_address addr=%#llx\n", a);
    fsm->address = a;

    break;
  }
  case DW_LNE_define_file:
    break;

  case DW_LNE_set_discriminator:
    break;
  }
  while (*offset - start_offset < ext_op_size)
    *offset += 1; // Skip rest
}

static void read_dwarf_section_debug_abbrev(pg_allocator_t allocator,
                                            uint8_t *data, uint64_t size,
                                            const struct section_64 *sec,
                                            dw_abbrev_t *abbrev) {
  assert(data != NULL);
  assert(sec != NULL);
  assert(abbrev != NULL);
  uint64_t offset = sec->offset;
  pg_array_init_reserve(abbrev->entries, 100, allocator);

  int tag_count = 0;
  while (offset < sec->offset + sec->size) {
    dw_abbrev_entry_t entry = {0};
    read_data(data, size, &offset, &entry.type, sizeof(entry.type));
    if (entry.type == 0)
      break;

    read_data(data, size, &offset, &entry.tag, sizeof(entry.tag));
    bool has_children = false;
    read_data(data, size, &offset, &has_children, sizeof(has_children));
    tag_count += 1;
    pg_log_debug(
        &logger, "[%d] .debug_abbrev: type_num=%d tag=%#x %s has_children=%d\n",
        tag_count, entry.type, entry.tag, dw_tag_str[entry.tag], has_children);

    pg_array_init_reserve(entry.attr_forms, 20, allocator);
    while (offset < sec->offset + sec->size) {
      dw_attr_form_t attr_form = {0};
      // TODO: check overflow
      attr_form.attr = (dw_attribute_t)(read_leb128_u64(data, size, &offset));
      attr_form.form = (dw_form_t)(read_leb128_u64(data, size, &offset));
      if (attr_form.attr == 0 && attr_form.form == 0)
        break;

      pg_log_debug(&logger, ".debug_abbrev: attr=%#x %s form=%#x %s\n",
                   attr_form.attr, dw_attribute_to_str(attr_form.attr),
                   attr_form.form, dw_form_str[attr_form.form]);

      pg_array_append(entry.attr_forms, attr_form);
    }
    pg_array_append(abbrev->entries, entry);
  }
}

static void read_dwarf_section_debug_info(pg_allocator_t allocator,
                                          uint8_t *data, uint64_t size,
                                          const struct section_64 *sec,
                                          const dw_abbrev_t *abbrev,
                                          debug_data_t *dd) {
  assert(data != NULL);
  assert(sec != NULL);
  assert(abbrev != NULL);
  assert(dd != NULL);

  uint64_t offset = sec->offset;

  pg_array_init_reserve(dd->fn_decls, 100, allocator);

  // TODO: multiple compile units (CU)
  uint32_t di_size = 0;
  read_data(data, size, &offset, &di_size, sizeof(di_size));
  pg_log_debug(&logger, ".debug_info size=%#x\n", di_size);

  uint16_t version = 0;
  read_data(data, size, &offset, &version, sizeof(version));
  pg_log_debug(&logger, ".debug_info version=%u\n", version);
  assert(version == 4);

  uint32_t abbr_offset = 0;
  read_data(data, size, &offset, &abbr_offset, sizeof(abbr_offset));
  pg_log_debug(&logger, ".debug_info abbr_offset=%#x\n", abbr_offset);

  uint8_t addr_size = 0;
  read_data(data, size, &offset, &addr_size, sizeof(addr_size));
  pg_log_debug(&logger, ".debug_info abbr_size=%#x\n", addr_size);

  char *directory = NULL;
  while (offset < sec->offset + sec->size) {
    uint8_t type = 0;
    read_data(data, size, &offset, &type, sizeof(type));
    assert(type <= pg_array_len(abbrev->entries));
    if (type == 0) {
      continue; // skip
    }

    const dw_abbrev_entry_t *entry = NULL;
    // TODO: pre-sort the entries to avoid the linear look-up each time?
    for (uint64_t i = 0; i < pg_array_len(abbrev->entries); i++) {
      if (abbrev->entries[i].type == type) {
        entry = &abbrev->entries[i];
        break;
      }
    }
    assert(entry != NULL);
    assert(entry->type == type);
    pg_log_debug(&logger, ".debug_info type=%#x tag=%#x %s\n", type, entry->tag,
                 dw_tag_str[entry->tag]);

    dw_fn_decl_t *se = NULL;
    if (entry->tag == DW_TAG_subprogram) {
      pg_array_append(dd->fn_decls, ((dw_fn_decl_t){.directory = directory}));
      const uint64_t se_count = pg_array_len(dd->fn_decls);
      se = &(dd->fn_decls)[se_count - 1];
    }
    if (entry->tag == DW_TAG_compile_unit) {
    }

    for (uint64_t i = 0; i < pg_array_len(entry->attr_forms); i++) {
      const dw_attr_form_t af = entry->attr_forms[i];
      pg_log_debug(&logger, ".debug_info: attr=%#x %s form=%#x %s\n", af.attr,
                   dw_attribute_to_str(af.attr), af.form, dw_form_str[af.form]);

      switch (af.form) {
      case DW_FORM_strp: {
        uint32_t str_offset = 0;
        read_data(data, size, &offset, &str_offset, sizeof(str_offset));
        pg_log_debug(&logger, "DW_FORM_strp: %#x ", str_offset);

        char *s = NULL;
        for (uint64_t j = 0; j < pg_array_len(dd->debug_str_strings); j++) {
          if (dd->debug_str_strings[j].offset == str_offset) {
            s = dd->debug_str_strings[j].s;
            break;
          }
        }
        assert(s != NULL);
        pg_log_debug(&logger, "%s\n", s);
        if (af.attr == DW_AT_name && se != NULL) {
          se->fn_name = s;
        }
        if (af.attr == DW_AT_comp_dir) {
          directory = s;
        }
        break;
      }
      case DW_FORM_data1: {
        uint8_t val = 0;
        read_data(data, size, &offset, &val, sizeof(val));
        pg_log_debug(&logger, "DW_FORM_data1: %#x\n", val);
        if (af.attr == DW_AT_decl_file && se != NULL && se->file == NULL) {
          assert(dd->debug_line_files != NULL);
          assert(val <= pg_array_len(dd->debug_line_files));
          se->file = dd->debug_line_files[val - 1];
        }
        break;
      }
      case DW_FORM_data2: {
        uint16_t lang = 0;
        read_data(data, size, &offset, &lang, sizeof(lang));
        pg_log_debug(&logger, "DW_FORM_data2: %#x\n", lang);
        break;
      }
      case DW_FORM_data4: {
        uint32_t val = 0;
        read_data(data, size, &offset, &val, sizeof(val));
        pg_log_debug(&logger, "DW_FORM_data4: %#x\n", val);
        if (af.attr == DW_AT_high_pc && se != NULL) {
          se->high_pc = (uint16_t)val;
        }
        break;
      }
      case DW_FORM_data8: {
        uint64_t val = 0;
        read_data(data, size, &offset, &val, sizeof(val));
        pg_log_debug(&logger, "DW_FORM_data8: %#llx\n", val);
        break;
      }
      case DW_FORM_ref1: {
        uint8_t val = 0;
        read_data(data, size, &offset, &val, sizeof(val));
        pg_log_debug(&logger, "DW_FORM_ref1: %#x\n", val);
        break;
      }
      case DW_FORM_ref2: {
        uint16_t val = 0;
        read_data(data, size, &offset, &val, sizeof(val));
        pg_log_debug(&logger, "DW_FORM_ref2: %#x\n", val);
        break;
      }
      case DW_FORM_ref4: {
        uint32_t val = 0;
        read_data(data, size, &offset, &val, sizeof(val));
        pg_log_debug(&logger, "DW_FORM_ref4: %#x\n", val);
        break;
      }
      case DW_FORM_ref8: {
        uint64_t val = 0;
        read_data(data, size, &offset, &val, sizeof(val));
        pg_log_debug(&logger, "DW_FORM_ref8: %#llx\n", val);
        break;
      }
      case DW_FORM_sec_offset: {
        uint32_t val = 0;
        read_data(data, size, &offset, &val, sizeof(val));
        pg_log_debug(&logger, "DW_FORM_sec_offset: %#x\n", val);
        break;
      }
      case DW_FORM_addr: {
        uint64_t addr = 0;
        read_data(data, size, &offset, &addr, sizeof(addr));
        pg_log_debug(&logger, "DW_FORM_addr: %#llx\n", addr);

        if (af.attr == DW_AT_low_pc && se != NULL) {
          se->low_pc = addr;
        }
        break;
      }
      case DW_FORM_flag_present: {
        pg_log_debug(&logger, "DW_FORM_flag_present (true)");
        break;
      }
      case DW_FORM_exprloc: {
        const uint64_t length = read_leb128_u64(data, size, &offset);
        offset += length;
        pg_log_debug(&logger, "DW_FORM_exprloc: length=%lld\n", length);

        break;
      }
      case DW_FORM_udata: {
        const uint64_t val = read_leb128_u64(data, size, &offset);
        pg_log_debug(&logger, "DW_FORM_udata: length=%lld\n", val);

        break;
      }
      case DW_FORM_sdata: {
        const int64_t val = read_leb128_i64(data, size, &offset);
        pg_log_debug(&logger, "DW_FORM_sdata: length=%lld\n", val);

        break;
      }

      default:
        assert(0 && "UNIMPLEMENTED");
      }
    }
  }
}

static void read_dwarf_section_debug_str(pg_allocator_t allocator,
                                         uint8_t *data, uint64_t size,
                                         const struct section_64 *sec,
                                         debug_data_t *dd) {
  assert(data != NULL);
  assert(sec != NULL);
  assert(dd != NULL);

  uint64_t offset = sec->offset;
  uint64_t i = 0;

  pg_array_init_reserve(dd->debug_str_strings, 100, allocator);
  while (offset < sec->offset + sec->size) {
    assert(offset < size);
    char *s = (char *)&data[offset];
    if (*s == 0) {
      offset++;
      continue;
    }

    assert(offset < size);
    char *end = memchr(&data[offset], 0, sec->offset + sec->size);
    assert(end != NULL);
    pg_log_debug(&logger, "- [%llu] %s\n", i, s);
    dw_string_t str = {.s = s, .offset = (uint32_t)(offset - sec->offset)};
    pg_array_append(dd->debug_str_strings, str);
    offset += (uint64_t)(end - s);
    i++;
  }
}

static bool dw_line_entry_should_add_new_entry(const dw_line_section_fsm_t *fsm,
                                               const debug_data_t *dd) {
  assert(fsm != NULL);
  assert(dd != NULL);

  if (fsm->line == 0)
    return false;
  const uint64_t count = pg_array_len(dd->line_entries);
  if (count == 0)
    return true;

  const dw_line_entry_t *last = &dd->line_entries[count - 1];
  if (last->pc == fsm->address)
    return false;

  if (last->line != fsm->line || last->file != fsm->file)
    return true;

  return false;
}

static void read_dwarf_section_debug_line(pg_allocator_t allocator,
                                          uint8_t *data, uint64_t size,
                                          const struct section_64 *sec,
                                          debug_data_t *dd) {
  assert(data != NULL);
  assert(sec != NULL);
  assert(dd != NULL);
  pg_array_init_reserve(dd->line_entries, 15000, allocator);

  uint64_t offset = sec->offset;
  dwarf_debug_line_header_t ddlh = {0};
  read_data(data, size, &offset, &ddlh, sizeof(ddlh));
  pg_log_debug(&logger,
               ".debug_line: length=%#x version=%#x header_length=%#x "
               "min_instruction_length=%#x max_ops_per_inst=%d "
               "default_is_stmt=%#x "
               "line_base=%d "
               "line_range=%d opcode_base=%d\n"
               ".debug_line: std_opcode_lengths[0]=%d\n"
               ".debug_line: std_opcode_lengths[1]=%d\n"
               ".debug_line: std_opcode_lengths[2]=%d\n"
               ".debug_line: std_opcode_lengths[3]=%d\n"
               ".debug_line: std_opcode_lengths[4]=%d\n"
               ".debug_line: std_opcode_lengths[5]=%d\n"
               ".debug_line: std_opcode_lengths[6]=%d\n"
               ".debug_line: std_opcode_lengths[7]=%d\n"
               ".debug_line: std_opcode_lengths[8]=%d\n"
               ".debug_line: std_opcode_lengths[9]=%d\n"
               ".debug_line: std_opcode_lengths[10]=%d\n"
               ".debug_line: std_opcode_lengths[11]=%d\n",
               ddlh.length, ddlh.version, ddlh.header_length,
               ddlh.min_instruction_length, ddlh.max_ops_per_inst,
               ddlh.default_is_stmt, ddlh.line_base, ddlh.line_range,
               ddlh.opcode_base, ddlh.std_opcode_lengths[0],
               ddlh.std_opcode_lengths[1], ddlh.std_opcode_lengths[2],
               ddlh.std_opcode_lengths[3], ddlh.std_opcode_lengths[4],
               ddlh.std_opcode_lengths[5], ddlh.std_opcode_lengths[6],
               ddlh.std_opcode_lengths[7], ddlh.std_opcode_lengths[8],
               ddlh.std_opcode_lengths[9], ddlh.std_opcode_lengths[10],
               ddlh.std_opcode_lengths[11]);

  assert(ddlh.line_range * ddlh.min_instruction_length != 0);

  assert(ddlh.version == 4);
  pg_log_debug(&logger, "Directories:");
  while (offset < sec->offset + sec->size) {
    assert(offset < size);
    char *s = (char *)&data[offset];
    assert(sec->offset + sec->size < size);
    char *end = memchr(&data[offset], 0, sec->offset + sec->size);
    assert(end != NULL);
    if (end - s == 0) {
      offset += 1;
      continue;
    }
    pg_log_debug(&logger, "- %s (%ld)\n", s, end - s);

    offset += (uint64_t)(end - s);
    if (*(end + 1) == 0) {
      offset += 2;
      break;
    }
  }
  pg_log_debug(&logger, "Files:");
  pg_array_init_reserve(dd->debug_line_files, 10, allocator);
  while (offset < sec->offset + sec->size) {
    assert(offset < size);
    char *s = (char *)&data[offset];
    if (*s == 0) {
      offset += 1;
      break;
    }
    assert(sec->offset + sec->size < size);
    char *end = memchr(&data[offset], 0, sec->offset + sec->size);
    assert(end != NULL);

    offset += (uint64_t)(end - s) + 1;
    const uint64_t dir_index = read_leb128_u64(data, size, &offset);
    const uint64_t modtime = read_leb128_u64(data, size, &offset);

    const uint64_t length = read_leb128_u64(data, size, &offset);

    pg_array_append(dd->debug_line_files, s);
    pg_log_debug(&logger,
                 "- %s dir_index=%llu modtime=%llu "
                 "length=%llu\n",
                 s, dir_index, modtime, length);
  }
  pg_log_debug(&logger, "");

  dw_line_section_fsm_t fsm = {.line = 1, .file = 1};

  while (offset < sec->offset + sec->size) {
    dw_lns_t opcode = 0;
    read_data(data, size, &offset, &opcode, sizeof(opcode));
    pg_log_debug(&logger,
                 "DW_OP=%#x offset=%#llx rel_offset=%#llx fsm.address=%#llx "
                 "fsm.line=%d "
                 "fsm.file=%llu\n",
                 opcode, offset, offset - sec->offset - 1, fsm.address,
                 fsm.line, fsm.file);
    switch (opcode) {
    case DW_LNS_extended_op: {
      const uint64_t ext_op_size = read_leb128_u64(data, size, &offset);
      pg_log_debug(&logger, "DW_LNS_extended_op size=%#llx\n", ext_op_size);

      read_dwarf_ext_op(data, size, &offset, &fsm, &dd->line_entries,
                        ext_op_size);
      break;
    }
    case DW_LNS_copy:
      pg_log_debug(&logger, "DW_LNS_copy");
      break;
    case DW_LNS_advance_pc: {
      const uint64_t decoded = read_leb128_u64(data, size, &offset);
      pg_log_debug(&logger, "DW_LNS_advance_pc leb128=%#llx\n", decoded);
      fsm.address += decoded;
      break;
    }
    case DW_LNS_advance_line: {
      const int64_t l = read_leb128_i64(data, size, &offset);
      fsm.line += l;
      pg_log_debug(&logger, "DW_LNS_advance_line line=%lld fsm.line=%hu\n", l,
                   fsm.line);
      if (dw_line_entry_should_add_new_entry(&fsm, dd)) {
        dw_line_entry_t e = {
            .pc = fsm.address, .line = fsm.line, .file = fsm.file};
        pg_log_debug(&logger,
                     "new dw_line_entry: pc=%#llx line=%d file=%llu %s\n", e.pc,
                     e.line, e.file, dd->debug_line_files[e.file - 1]);
        pg_array_append(dd->line_entries, e);
      }
      break;
    }
    case DW_LNS_set_file:
      fsm.file = read_leb128_u64(data, size, &offset);
      pg_log_debug(&logger, "DW_LNS_set_file file=%llu\n", fsm.file);
      break;
    case DW_LNS_set_column: {
      const uint64_t column = read_leb128_u64(data, size, &offset);
      pg_log_debug(&logger, "DW_LNS_set_column column=%llu\n", column);
      break;
    }
    case DW_LNS_negate_stmt:
      pg_log_debug(&logger, "DW_LNS_negate_stmt");
      fsm.is_stmt = !fsm.is_stmt;
      break;
    case DW_LNS_set_basic_block:
      break;
    case DW_LNS_const_add_pc: {
      const uint8_t op = 255 - ddlh.opcode_base;
      fsm.address += op / ddlh.line_range * ddlh.min_instruction_length;
      // TODO: op_index
      pg_log_debug(&logger, "address+=%#x -> address=%#llx\n",
                   op / ddlh.line_range * ddlh.min_instruction_length,
                   fsm.address);
      break;
    }
    case DW_LNS_fixed_advance_pc:
      break;
    case DW_LNS_set_prologue_end:
      pg_log_debug(&logger, "DW_LNS_set_prologue_end");
      break;
    case DW_LNS_set_epilogue_begin:
      break;
    case DW_LNS_set_isa:
      break;
    default: {
      const uint8_t op = opcode - ddlh.opcode_base;
      fsm.address += op / ddlh.line_range * ddlh.min_instruction_length;

      fsm.line += ddlh.line_base + op % ddlh.line_range;
      if (dw_line_entry_should_add_new_entry(&fsm, dd)) {
        dw_line_entry_t e = {
            .pc = fsm.address, .line = fsm.line, .file = fsm.file};
        pg_log_debug(&logger,
                     "new dw_line_entry: pc=%#llx line=%d file=%llu %s\n", e.pc,
                     e.line, e.file, dd->debug_line_files[e.file - 1]);
        pg_array_append(dd->line_entries, e);
      }
      pg_log_debug(&logger, "address+=%d line+=%d\n",
                   op / ddlh.line_range * ddlh.min_instruction_length,
                   ddlh.line_base + op % ddlh.line_range);
    }
    }
  }
}

static void stacktrace_find_entry(const debug_data_t *dd, uint64_t pc,
                                  stacktrace_entry_t *se) {
  for (uint64_t i = 0; i < pg_array_len(dd->fn_decls); i++) {
    const dw_fn_decl_t *fd = &dd->fn_decls[i];
    if (fd->low_pc <= pc && pc <= fd->low_pc + fd->high_pc) {
      se->directory = fd->directory;
      se->file = fd->file;
      se->fn_name = fd->fn_name;
      break;
    }
  }
  if (se->directory == NULL)
    return;
  assert(se->file != NULL);
  assert(se->fn_name != NULL);

  const dw_line_entry_t *cur_le = NULL;
  const dw_line_entry_t *prev_le = NULL;
  for (uint64_t i = 0; i < pg_array_len(dd->line_entries) - 1; i++) {
    prev_le = &dd->line_entries[i];
    cur_le = &dd->line_entries[i + 1];
    if (prev_le->file == cur_le->file) {
      if (!(prev_le->pc < cur_le->pc)) {
        pg_log_debug(&logger, "[D009] i=%llu\n", i);
      }
      assert(prev_le->pc < cur_le->pc); // Shoud be sorted
    }
    if (cur_le->pc > pc) {
      se->line = prev_le->line;
      break;
    }
  }
}

static void read_source_code(pg_allocator_t allocator, debug_data_t *dd) {
  assert(dd != NULL);
  static char path[PATH_MAX + 1] = "";

  pg_array_init_reserve(dd->sources, 100, allocator);
  for (uint64_t i = 0; i < pg_array_len(dd->fn_decls); i++) {
    dw_fn_decl_t *fd = &(dd->fn_decls)[i];
    if (fd->directory == NULL || fd->file == NULL)
      continue;

    snprintf(path, sizeof(path), "%s/%s", fd->directory, fd->file);
    pg_array_t(uint8_t) contents = {0};
    pg_array_init_reserve(contents, 0, pg_heap_allocator());
    if (!pg_read_file(path, &contents)) {
      fprintf(stderr, "Failed to read file: %s %s\n", path, strerror(errno));
      exit(errno);
    }

    source_file_t source = {
        .contents = contents, .directory = fd->directory, .file = fd->file};
    pg_array_init_reserve(source.newline_offsets, 500, allocator);

    pg_array_append(source.newline_offsets, 0);
    for (uint64_t j = 0; j < pg_array_len(contents); j++) {
      uint8_t *c = &contents[j];
      if (*c == '\n')
        pg_array_append(source.newline_offsets, (uint16_t)j);
    }
    pg_array_append(source.newline_offsets,
                    (uint16_t)pg_array_len(source.contents));

    pg_array_append(dd->sources, source);
  }
}

extern uint64_t get_main_address(void);
__asm__(".globl _get_main_address\n\t"
        "_get_main_address:\n\t"
        "lea _main(%rip), %rax\n\t"
        "ret\n\t");

static void read_macho_dsym(pg_allocator_t allocator, uint8_t *data,
                            uint64_t size, debug_data_t *dd) {
  uint64_t offset = 0;
  struct mach_header_64 h = {0};
  read_data(data, size, &offset, &h, sizeof(h));
  assert(h.filetype == MH_DSYM);

  pg_log_debug(&logger,
               "magic=%d\ncputype=%d\ncpusubtype=%d\nfiletype=%d\nncmds=%"
               "d\nsizeofcmds=%d\nflags=%d\n",
               h.magic, h.cputype, h.cpusubtype, h.filetype, h.ncmds,
               h.sizeofcmds, h.flags);

  // Remember where those sections were since they might be
  // out-of-order and we need to first read the abbrev section & str section,
  // and then the info section.
  struct section_64 sec_abbrev = {0};
  struct section_64 sec_info = {0};
  struct section_64 sec_str = {0};
  struct section_64 sec_line = {0};

  for (uint64_t cmd_count = 0; cmd_count < h.ncmds; cmd_count++) {
    struct load_command c = {0};
    read_data(data, size, &offset, &c, sizeof(c));
    pg_log_debug(&logger, "command: cmd=%d cmdsize=%d\n", c.cmd, c.cmdsize);
    offset -= sizeof(c);

    switch (c.cmd) {
    case LC_UUID: {
      struct uuid_command uc = {0};
      read_data(data, size, &offset, &uc, sizeof(uc));
      pg_log_debug(&logger,
                   "LC_UUID uuid=%#x %#x %#x %#x %#x %#x %#x %#x %#x %#x "
                   "%#x "
                   "%#x %#x "
                   "%#x "
                   "%#x %#x\n",
                   uc.uuid[0], uc.uuid[1], uc.uuid[2], uc.uuid[3], uc.uuid[4],
                   uc.uuid[5], uc.uuid[6], uc.uuid[7], uc.uuid[8], uc.uuid[9],
                   uc.uuid[10], uc.uuid[11], uc.uuid[12], uc.uuid[13],
                   uc.uuid[14], uc.uuid[15]);

      break;
    }
    case LC_BUILD_VERSION: {
      struct build_version_command vc = {0};
      read_data(data, size, &offset, &vc, sizeof(vc));
      pg_log_debug(&logger,
                   "LC_BUILD_VERSION platform=%#x minos=%#x sdk=%#x "
                   "ntools=%d\n",
                   vc.platform, vc.minos, vc.sdk, vc.ntools);

      assert(vc.ntools == 0 && "UNIMPLEMENTED");
      break;
    }
    case LC_SYMTAB: {
      struct symtab_command sc = {0};
      read_data(data, size, &offset, &sc, sizeof(sc));

      pg_log_debug(&logger,
                   "LC_SYMTAB symoff=%#x nsyms=%d stroff=%#x strsize=%d\n",
                   sc.symoff, sc.nsyms, sc.stroff, sc.strsize);

      break;
    }
    case LC_SEGMENT_64: {
      struct segment_command_64 sc = {0};
      read_data(data, size, &offset, &sc, sizeof(sc));

      pg_log_debug(&logger,
                   "LC_SEGMENT_64 segname=%s vmaddr=%#llx vmsize=%#llx "
                   "fileoff=%#llx filesize=%#llx maxprot=%#x initprot=%#x "
                   "nsects=%d flags=%d\n",
                   sc.segname, sc.vmaddr, sc.vmsize, sc.fileoff, sc.filesize,
                   sc.maxprot, sc.initprot, sc.nsects, sc.flags);

      for (uint64_t sec_count = 0; sec_count < sc.nsects; sec_count++) {
        struct section_64 sec = {0};
        read_data(data, size, &offset, &sec, sizeof(sec));
        pg_log_debug(&logger,
                     "SECTION sectname=%s segname=%s addr=%#llx "
                     "size=%#llx "
                     "offset=%#x align=%#x reloff=%#x nreloc=%d "
                     "flags=%#x\n",
                     sec.sectname, sec.segname, sec.addr, sec.size, sec.offset,
                     sec.align, sec.reloff, sec.nreloc, sec.flags);

        if (strcmp(sec.sectname, "__debug_line") == 0) {
          sec_line = sec;
        } else if (strcmp(sec.sectname, "__debug_str") == 0) {
          sec_str = sec;
        } else if (strcmp(sec.sectname, "__debug_info") == 0) {
          sec_info = sec;
        } else if (strcmp(sec.sectname, "__debug_abbrev") == 0) {
          sec_abbrev = sec;
        }
      }

      break;
    }
    default:
      assert(0 && "UNIMPLEMENTED - catch all");
    }
  }
  assert(sec_abbrev.sectname[0] != 0);
  assert(sec_info.sectname[0] != 0);
  assert(sec_str.sectname[0] != 0);
  assert(sec_line.sectname[0] != 0);

  read_dwarf_section_debug_str(allocator, data, size, &sec_str, dd);
  dw_abbrev_t abbrev = {0};
  read_dwarf_section_debug_line(allocator, data, size, &sec_line, dd);
  read_dwarf_section_debug_abbrev(allocator, data, size, &sec_abbrev, &abbrev);

  read_dwarf_section_debug_info(allocator, data, size, &sec_info, &abbrev, dd);

  read_source_code(allocator, dd);

  for (uint64_t i = 0; i < pg_array_len(dd->fn_decls); i++) {
    dw_fn_decl_t *fd = &(dd->fn_decls)[i];
    pg_log_debug(&logger,
                 "dw_fn_decl: low_pc=%#llx high_pc=%#hx fn_name=%s "
                 "file=%s/%s\n",
                 fd->low_pc, fd->high_pc, fd->fn_name, fd->directory, fd->file);

    if (fd->fn_name != NULL && strcmp(fd->fn_name, "main") == 0) {
      dd->pie_displacement = get_main_address() - fd->low_pc;
      pg_log_debug(&logger, "pie_displacement=%#llx\n", dd->pie_displacement);
    }
  }

  for (uint64_t i = 0; i < pg_array_len(dd->line_entries); i++) {
    dw_line_entry_t *le = &(dd->line_entries)[i];
    pg_log_debug(
        &logger, "dw_line_entry[%llu]: line=%d pc=%#llx file=%llu %s\n", i,
        le->line, le->pc, le->file, dd->debug_line_files[le->file - 1]);
  }
}

static char *get_exe_path_for_process(void) {
  static char pathbuf[PROC_PIDPATHINFO_MAXSIZE] = "";
  if (pathbuf[0] != 0)
    return pathbuf;

  int res = proc_pidpath(getpid(), pathbuf, sizeof(pathbuf));
  if (res <= 0) {
    fprintf(stderr,
            "Failed to get the path of the executable for the current "
            "process: %s\n",
            strerror(errno));
    exit(errno);
  }
  return pathbuf;
}

typedef enum {
  COL_RESET,
  COL_GRAY,
  COL_RED,
  COL_GREEN,
  COL_COUNT,
} pg_color_t;

static const char pg_colors[2][COL_COUNT][14] = {
    // is_tty == true
    [true] = {[COL_RESET] = "\x1b[0m",
              [COL_GRAY] = "\x1b[38;5;243m",
              [COL_RED] = "\x1b[31m",
              [COL_GREEN] = "\x1b[32m"}};

__attribute__((unused)) static void stacktrace_print(void) {
  static bool is_tty = false;
  is_tty = isatty(2);

  static debug_data_t dd = {0};
  if (dd.debug_str_strings == NULL) { // Not yet parsed the debug information?
    pg_allocator_t allocator = pg_heap_allocator();
    char path[PATH_MAX + 1] = "";
    const char *exe_path = get_exe_path_for_process();
    const char *exe_name = pg_path_base_name(exe_path);
    snprintf(path, sizeof(path), "%s.dSYM/Contents/Resources/DWARF/%s",
             exe_name, exe_name);
    pg_array_t(uint8_t) contents = {0};
    pg_array_init_reserve(contents, 0, pg_heap_allocator());
    if (!pg_read_file(path, &contents)) {
      pg_log_fatal(&logger, errno, "Failed to read file: %s %s\n", path,
                   strerror(errno));
    }
    read_macho_dsym(allocator, contents, pg_array_len(contents), &dd);
  }

  uintptr_t *rbp = __builtin_frame_address(0);
  while (rbp != 0 && *rbp != 0) {
    uintptr_t rip = *(rbp + 1);
    pg_log_debug(&logger, "rbp=%p rip=%#lx rsp=%#lx\n", (void *)rbp, rip,
                 *(rbp + 2));
    rbp = (uintptr_t *)*rbp;

    stacktrace_entry_t se = {0};
    stacktrace_find_entry(
        &dd, rip - /* `call` instruction size */ 5 - dd.pie_displacement, &se);
    if (se.directory != NULL) {
      printf("%s%#lx %s/%s:%s:%llu%s", pg_colors[is_tty][COL_GRAY], rip,
             se.directory, se.file, se.fn_name, se.line,
             pg_colors[is_tty][COL_RESET]);

      source_file_t *f = NULL;
      for (uint64_t i = 0; i < pg_array_len(dd.sources); i++) {
        source_file_t *sf = &dd.sources[i];
        if (strcmp(sf->directory, se.directory) == 0 &&
            strcmp(sf->file, se.file) == 0) {
          f = sf;
          break;
        }
      }

      assert(se.line >= 1);
      assert(se.line - 1 < pg_array_len(f->newline_offsets));
      uint32_t offset_start = f->newline_offsets[se.line - 1] + 1;

      assert(se.line < pg_array_len(f->newline_offsets));
      uint32_t offset_end = f->newline_offsets[se.line];

      assert(offset_start < pg_array_len(f->contents));
      assert(offset_end < pg_array_len(f->contents));
      char *source_line = (char *)&f->contents[offset_start];
      uint32_t source_line_len = offset_end - offset_start;

      pg_log_debug(&logger,
                   "se.line=%llu offset_start=%d offset_end=%d len=%d\n",
                   se.line, offset_start, offset_end, source_line_len);

      // Trim left
      while (*source_line != 0 && pg_char_is_space(*source_line)) {
        source_line++;
        source_line_len--;
      }
      printf(":\t\t%.*s\n", source_line_len, source_line);
    }
  }
}
