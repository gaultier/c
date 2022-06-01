#include <assert.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <stab.h>
#define GB_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>

#include "vendor/gb.h"

int main(int argc, const char* argv[]) {
    assert(argc == 2);
    const char* path = argv[1];

    FILE* f = fopen(path, "r");
    assert(f);

    struct mach_header_64 h = {0};
    assert(fread(&h, sizeof(h), 1, f) >= 0);
    assert(h.cputype == CPU_TYPE_X86_64);
    assert(h.filetype == MH_DSYM);

    printf(
        "magic=%d\ncputype=%d\ncpusubtype=%d\nfiletype=%d\nncmds=%"
        "d\nsizeofcmds=%d\nflags=%d\n",
        h.magic, h.cputype, h.cpusubtype, h.filetype, h.ncmds, h.sizeofcmds,
        h.flags);

    for (int cmd_count = 0; cmd_count < h.ncmds; cmd_count++) {
        struct load_command c = {0};
        assert(fread(&c, sizeof(c), 1, f) >= 0);
        printf("command: cmd=%d cmdsize=%d\n", c.cmd, c.cmdsize);

        switch (c.cmd) {
            case LC_UUID: {
                struct uuid_command uc = {0};
                assert(fread(&uc.uuid,
                             sizeof(uc) - sizeof(uc.cmd) - sizeof(uc.cmdsize),
                             1, f) >= 0);
                printf(
                    "LC_UUID uuid=%#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x "
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
                assert(fread(&vc.platform,
                             sizeof(vc) - sizeof(vc.cmd) - sizeof(vc.cmdsize),
                             1, f) >= 0);
                printf(
                    "LC_BUILD_VERSION platform=%#x minos=%#x sdk=%#x "
                    "ntools=%d\n",
                    vc.platform, vc.minos, vc.sdk, vc.ntools);

                assert(vc.ntools == 0 && "UNIMPLEMENTED");
                break;
            }
            case LC_SYMTAB: {
                struct symtab_command sc = {0};
                assert(fread(&sc.symoff,
                             sizeof(sc) - sizeof(sc.cmd) - sizeof(sc.cmdsize),
                             1, f) >= 0);
                printf("LC_SYMTAB symoff=%#x nsyms=%d stroff=%#x strsize=%d\n",
                       sc.symoff, sc.nsyms, sc.stroff, sc.strsize);
                break;
            }
            case LC_SEGMENT_64: {
                struct segment_command_64 sc = {0};
                assert(fread(&sc.segname,
                             sizeof(sc) - sizeof(sc.cmd) - sizeof(sc.cmdsize),
                             1, f) >= 0);
                printf(
                    "LC_SEGMENT_64 segname=%16s vmaddr=%#llx vmsize=%#llx "
                    "fileoff=%#llx filesize=%#llx maxprot=%#x initprot=%#x "
                    "nsects=%d flags=%d\n",
                    sc.segname, sc.vmaddr, sc.vmsize, sc.fileoff, sc.filesize,
                    sc.maxprot, sc.initprot, sc.nsects, sc.flags);
                break;
            }
            default:
                assert(0 && "UNIMPLEMENTED - catch all");
        }
    }
}
