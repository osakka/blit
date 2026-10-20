#include "../../src/blit_cell_cluster.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "CELL_CLUSTER_FAIL line=%d\n", __LINE__); return 1; } } while (0)

int main(int argc, char **argv)
{
    uint32_t scalars[BLIT_CLUSTER_MAX_SCALARS], bad[] = {0xd800U};
    BlitCellCluster key, same, shorter, collision, saved;
    unsigned length, i, columns, rows = 0;
    CHECK(argc == 2);
    while (scanf("%u", &length) == 1) {
        CHECK(length > 0 && length <= BLIT_CLUSTER_MAX_SCALARS);
        for (i = 0; i < length; ++i) CHECK(scanf("%x", &scalars[i]) == 1);
        for (columns = 1; columns <= 2; ++columns) {
            CHECK(blit_cell_cluster_init(&key, scalars[0], scalars + 1, length - 1, columns));
            CHECK(key.length == length && key.columns == columns);
            CHECK(!memcmp(key.scalars, scalars, length * sizeof(uint32_t)));
            CHECK(blit_cell_cluster_init(&same, scalars[0], scalars + 1, length - 1, columns));
            CHECK(blit_cell_cluster_equal(&key, &same));
            CHECK(blit_cell_cluster_hash(&key) == blit_cell_cluster_hash(&same));
            same.columns = columns == 1 ? 2 : 1;
            CHECK(!blit_cell_cluster_equal(&key, &same));
            if (length > 1) {
                CHECK(blit_cell_cluster_init(&shorter, scalars[0], scalars + 1, length - 2, columns));
                CHECK(!blit_cell_cluster_equal(&key, &shorter));
            }
        }
        ++rows;
    }
    CHECK(rows == (unsigned)atoi(argv[1]));
    for (i = 0; i < BLIT_CLUSTER_MAX_SCALARS; ++i) scalars[i] = 0x300 + i;
    CHECK(blit_cell_cluster_init(&key, 'A', scalars, BLIT_MAX_COMBINING, 2));
    memcpy(&saved, &key, sizeof(key));
    scalars[0] = 0x400;
    CHECK(key.scalars[1] == 0x300); /* Value owns its data. */
    CHECK(!blit_cell_cluster_init(&key, 'A', scalars, BLIT_MAX_COMBINING + 1, 2));
    CHECK(!blit_cell_cluster_init(&key, 'A', NULL, 1, 2));
    CHECK(!blit_cell_cluster_init(&key, 0x110000, NULL, 0, 2));
    CHECK(!blit_cell_cluster_init(&key, 'A', bad, 1, 2));
    CHECK(!blit_cell_cluster_init(&key, 'A', NULL, 0, 0));
    CHECK(!blit_cell_cluster_init(&key, 'A', NULL, 0, 3));
    CHECK(!memcmp(&key, &saved, sizeof(key))); /* Rejections are atomic. */
    CHECK(blit_cell_cluster_init(&key, 'A', NULL, 0, 1));
    CHECK(blit_cell_cluster_init(&collision, 'A' + 128, NULL, 0, 1));
    CHECK(blit_cell_cluster_hash(&key) % 128 == blit_cell_cluster_hash(&collision) % 128);
    CHECK(!blit_cell_cluster_equal(&key, &collision));
    same = key; same.columns = 0;
    CHECK(!blit_cell_cluster_equal(&same, &same) && !blit_cell_cluster_hash(&same));
    same = key; same.length = BLIT_CLUSTER_MAX_SCALARS + 1;
    CHECK(!blit_cell_cluster_equal(&same, &same) && !blit_cell_cluster_hash(&same));
    CHECK(!blit_cell_cluster_hash(NULL) && !blit_cell_cluster_equal(NULL, NULL));
    printf("CELL_CLUSTER_PASS corpus=%u widths=owner_1_and_2 max_attachments=%d atomic_rejection=all cache_collision=distinct scope=value_contract_not_shaping\n",
        rows, BLIT_MAX_COMBINING);
    return 0;
}
