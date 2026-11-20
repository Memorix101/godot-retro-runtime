/* rpkdump: prints the content of a package as text.
 *
 *     rpkdump scene.rpk
 *
 * Runs on the PC and reads packages of any little-endian platform. The output format is
 * shared with the runtime's --dump flag (runtime/core/rr_dump.c). */
#include <stdio.h>
#include <stdlib.h>

#include "../runtime/core/rr_platform.h"

int main(int argc, char **argv)
{
    RRPackage pkg;
    uint32_t  size = 0;
    void     *data;

    if (argc != 2) {
        fprintf(stderr, "usage: rpkdump <package.rpk>\n");
        return 2;
    }
    data = rr_plat_read_file(argv[1], &size);
    if (data == NULL || !rr_package_open(&pkg, data, size, RR_PLATFORM_ANY)) {
        fprintf(stderr, "rpkdump: %s: %s\n", argv[1], rr_last_error());
        free(data);
        return 1;
    }
    rr_package_dump(&pkg, stdout);
    free(data);
    return 0;
}
