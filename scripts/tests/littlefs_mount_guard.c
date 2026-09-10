// Exercise the actual pinned LittleFS code with an in-memory flash device.
#include "lfs.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char disk[128 * 4096];
static unsigned writes;
#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "check failed line %d: %s\n", __LINE__, #x); \
        exit(1); \
    } \
} while (0)

static int read_block(const struct lfs_config *c, lfs_block_t b, lfs_off_t o,
                      void *p, lfs_size_t n) {
    (void)c;
    if (b >= 128 || o + n > 4096) return LFS_ERR_IO;
    memcpy(p, disk + b * 4096 + o, n);
    return 0;
}

static int write_block(const struct lfs_config *c, lfs_block_t b, lfs_off_t o,
                       const void *p, lfs_size_t n) {
    (void)c;
    if (b >= 128 || o + n > 4096) return LFS_ERR_IO;
    writes++;
    for (unsigned i = 0; i < n; i++) {
        disk[b * 4096 + o + i] &= ((const unsigned char *)p)[i];
    }
    return 0;
}

static int erase_block(const struct lfs_config *c, lfs_block_t b) {
    (void)c;
    if (b >= 128) return LFS_ERR_IO;
    writes++;
    memset(disk + b * 4096, 255, 4096);
    return 0;
}

static int sync_disk(const struct lfs_config *c) {
    (void)c;
    return 0;
}

static struct lfs_config cfg = {
    .read = read_block, .prog = write_block, .erase = erase_block,
    .sync = sync_disk, .read_size = 128, .prog_size = 128,
    .block_size = 4096, .block_count = 128, .cache_size = 512,
    .lookahead_size = 128, .block_cycles = 512,
};

static void fresh(lfs_t *lfs) {
    memset(disk, 255, sizeof(disk));
    memset(lfs, 0, sizeof(*lfs));
    cfg.block_count = 128;
    CHECK(lfs_format(lfs, &cfg) == 0);
    CHECK(lfs_mount(lfs, &cfg) == 0);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    lfs_t lfs = {0};
    const char *mode = argv[1];
    if (!strcmp(mode, "blank") || !strcmp(mode, "zeros")) {
        memset(disk, !strcmp(mode, "blank") ? 255 : 0, sizeof(disk));
    } else {
        fresh(&lfs);
        if (!strcmp(mode, "valid")) {
            lfs_file_t file = {0};
            CHECK(lfs_file_open(&lfs, &file, "test", LFS_O_CREAT | LFS_O_WRONLY) == 0);
            CHECK(lfs_file_write(&lfs, &file, "ok", 2) == 2);
            CHECK(lfs_file_close(&lfs, &file) == 0);
        } else {
            // Build CRC-valid but semantically invalid metadata. Random bit
            // corruption alone would exercise CRC rejection, not this bug.
            lfs_mdir_t dir;
            CHECK(lfs_dir_fetch(&lfs, &dir, lfs.root) == 0);
            if (!strcmp(mode, "missing") || !strcmp(mode, "missing-fixed")) {
                CHECK(lfs_dir_commit(&lfs, &dir, LFS_MKATTRS(
                    {LFS_MKTAG(LFS_TYPE_SUPERBLOCK, 0, 0x3ff), NULL})) == 0);
            } else {
                lfs_superblock_t superblock = {
                    .version = lfs_fs_disk_version(&lfs), .block_size = 4096,
                    .block_count = !strcmp(mode, "one") ? 1 : 0,
                    .name_max = 255, .file_max = LFS_FILE_MAX,
                    .attr_max = LFS_ATTR_MAX,
                };
                lfs_superblock_tole32(&superblock);
                CHECK(lfs_dir_commit(&lfs, &dir, LFS_MKATTRS(
                    {LFS_MKTAG(LFS_TYPE_INLINESTRUCT, 0, sizeof(superblock)),
                     &superblock})) == 0);
            }
        }
        CHECK(lfs_unmount(&lfs) == 0);
    }

    cfg.block_count = !strcmp(mode, "missing-fixed") ? 128 : 0;
    writes = 0;
    int result = lfs_mount(&lfs, &cfg);
    CHECK(writes == 0);
    if (!strcmp(mode, "valid")) {
        CHECK(result == 0);
        CHECK(lfs.block_count == 128);
        lfs_file_t file = {0};
        char bytes[2];
        CHECK(lfs_file_open(&lfs, &file, "test", LFS_O_RDONLY) == 0);
        CHECK(lfs_file_read(&lfs, &file, bytes, 2) == 2);
        CHECK(memcmp(bytes, "ok", 2) == 0);
        CHECK(lfs_file_close(&lfs, &file) == 0);
        CHECK(lfs_unmount(&lfs) == 0);
    } else {
        CHECK(result == LFS_ERR_CORRUPT);
    }
    printf("%s: passed (mount=%d, no mount writes)\n", mode, result);
    return 0;
}
