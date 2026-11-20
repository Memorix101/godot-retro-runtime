/* rr_plat_read_file for every platform whose C library has working stdio
 * (PC, and Dreamcast through KallistiOS). */
#include <stdio.h>
#include <stdlib.h>

#include "../core/rr_platform.h"

void *rr_plat_read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    long  length;
    void *block;

    if (f == NULL) {
        rr_set_error("cannot open '%s'", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (length <= 0) {
        fclose(f);
        rr_set_error("'%s' is empty or unreadable", path);
        return NULL;
    }
    block = malloc((size_t)length);
    if (block == NULL || fread(block, 1, (size_t)length, f) != (size_t)length) {
        free(block);
        fclose(f);
        rr_set_error("cannot read %ld bytes from '%s'", length, path);
        return NULL;
    }
    fclose(f);
    *size = (uint32_t)length;
    return block;
}
