#ifndef HT_H
#define HT_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HASH_TABLE_NOT_FOUND SIZE_MAX

#if !defined(HASH_MODULOR) || (HASH_MODULOR <= 0)
#error "HASH_MODULOR macro must be defined as a positive number" \
       "It can be defined by -DHASH_MODULOR=xxx in cmdline"
#endif

struct dict_t {
    const char *str;
    void *val;
};

void *ht_get(struct dict_t *, const char *, size_t);
size_t ht_add(struct dict_t *, const char *, size_t, void *);
size_t ht_del(struct dict_t *, const char *, size_t);
#endif // HT_H

#ifdef HASH_TABLE_IMPLEMENTATION

size_t str_hash(const char *str, size_t len) {
    size_t hash = 0;
    for (size_t i = 0; i < len; ++i) {
        hash = hash * 131 + *(str + i);
    }
    return hash % HASH_MODULOR;
}

static const char *deleted_flag = "DELETED";
void *ht_get(struct dict_t *table, const char *str, size_t len) {
    size_t hash = str_hash(str, len);
    size_t i = hash;
    // 只要不为NULL就继续寻找
    while (table[i].str != NULL) {
        if (i == ((int)hash - 1 + HASH_MODULOR) % HASH_MODULOR) {
            break;
        }
        // 如果被标为删除就跳过
        if (table[i].str == deleted_flag ||
            strncmp(table[i].str, str, len) != 0) {
            i = (i + 1) % HASH_MODULOR;
            continue;
        }
        return table[i].val;
    }
    return NULL;
}

size_t ht_add(struct dict_t *table, const char *str, size_t len, void *val) {
    size_t hash = str_hash(str, len);
    size_t i = hash;
    // 寻找一个空位或者已经删除的位置
    while (table[i].str != NULL && table[i].str != deleted_flag) {
        // 如果循环回来代表没有空位
        if (i == ((int)hash - 1 + HASH_MODULOR) % HASH_MODULOR) {
            return HASH_TABLE_NOT_FOUND;
        }
        i = (i + 1) % HASH_MODULOR;
    }
    table[i].str = str;
    table[i].val = val;
    return i;
}

size_t ht_del(struct dict_t *table, const char *str, size_t len) {
    size_t hash = str_hash(str, len);
    size_t i = hash;
    // 只要不为NULL就继续
    while (table[i].str != NULL) {
        if (i == ((int)hash - 1 + HASH_MODULOR) % HASH_MODULOR) {
            break;
        }
        // 如果已经被删除就跳过
        if (table[i].str == deleted_flag ||
            strncmp(table[i].str, str, len) != 0) {
            i = (i + 1) % HASH_MODULOR;
            continue;
        }
        return i;
    }
    return HASH_TABLE_NOT_FOUND;
}

#endif // HASH_TABLE_IMPLEMENTATION
