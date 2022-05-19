#include "fs_watch.h"

int main(int argc, char* argv[]) {
    gbString path = gb_make_string(argv[1]);
    fs_watch_file(&path);
}
