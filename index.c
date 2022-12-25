#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

//errors checked
// ─── PROVIDED ────────────────────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    return -1;
}

// ─── IMPLEMENTATION ─────────────────────────────────────────────────────────

// LOAD INDEX
int index_load(Index *index) {
    FILE *f = fopen(".pes/index", "r");
    if (!f) {
        index->count = 0;
        return 0;
    }

    index->count = 0;

    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];

        char hash_hex[HASH_HEX_SIZE + 1];

        int ret = fscanf(f, "%o %64s %ld %ld %255s\n",
                         &e->mode,
                         hash_hex,
                         &e->mtime_sec,
                         &e->size,
                         e->path);

        if (ret != 5) break;

        hex_to_hash(hash_hex, &e->hash);

        index->count++;
    }

    fclose(f);
    return 0;
}

// SORT HELPER
static int compare_index_entries(const void *a, const void *b) {
    return strcmp(((const IndexEntry *)a)->path,
                  ((const IndexEntry *)b)->path);
}

// SAVE INDEX
int index_save(const Index *index) {
    char tmp_path[] = ".pes/index.tmp";

    FILE *f = fopen(tmp_path, "w");
    if (!f) return -1;

    Index sorted = *index;
    qsort(sorted.entries, sorted.count, sizeof(IndexEntry), compare_index_entries);

    for (int i = 0; i < sorted.count; i++) {
        const IndexEntry *e = &sorted.entries[i];

        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&e->hash, hex);

        fprintf(f, "%o %s %ld %ld %s\n",
                e->mode,
                hex,
                e->mtime_sec,
                e->size,
                e->path);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    rename(tmp_path, ".pes/index");

    return 0;
}

// ADD FILE TO INDEX
int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    void *buffer = malloc(st.st_size);
    fread(buffer, 1, st.st_size, f);
    fclose(f);

    ObjectID id;
    extern int object_write(ObjectType, const void *, size_t, ObjectID *);

    if (object_write(OBJ_BLOB, buffer, st.st_size, &id) != 0) {
        free(buffer);
        return -1;
    }

    free(buffer);

    IndexEntry *e = index_find(index, path);
    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        e = &index->entries[index->count++];
    }

    e->mode = (st.st_mode & S_IXUSR) ? 0100755 : 0100644;
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;
    e->hash = id;

    strncpy(e->path, path, sizeof(e->path));
    e->path[sizeof(e->path) - 1] = '\0';

    return index_save(index);
}
