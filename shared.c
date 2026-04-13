#include "shared.h"
#include <stdlib.h>
#include <stdio.h>

#define LIST_IMPLEMENTATION
#include "list.h"
#define HASH_TABLE_IMPLEMENTATION
#define HASH_MODULOR 101
#include "ht.h"

#define DEF_LOG_FUNC(type, color)                                              \
    void log_##type(const char *fmt, ...) {                             \
        va_list ap;                                                            \
                                                                               \
        fprintf(stderr, color "[" #type "]" COLOR_RESET "\t");                 \
                                                                               \
        va_start(ap, fmt);                                                     \
        vfprintf(stderr, fmt, ap);                                             \
        va_end(ap);                                                            \
                                                                               \
        fputc('\n', stderr);                                                   \
    }

DEF_LOG_FUNC(info, COLOR_CYAN)
DEF_LOG_FUNC(debug, COLOR_GREEN)
DEF_LOG_FUNC(warn, COLOR_YELLOW)
DEF_LOG_FUNC(error, COLOR_RED)

struct dict_t sni_name_to_obj[TABLE_LEN];
struct list_t snis;
struct list_t snhs;

struct list_t *get_icon(DBusMessageIter *iter) {
    struct list_t *pixmaps = malloc(sizeof(struct list_t));
    list_init(pixmaps);
    DBusMessageIter array_iter, struct_iter, inner_array_iter;
    dbus_message_iter_recurse(iter, &array_iter);

    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRUCT) {
        pixmap_t *p = malloc(sizeof(pixmap_t));
        dbus_message_iter_recurse(&array_iter, &struct_iter);

        dbus_message_iter_get_basic(&struct_iter, &p->w);
        dbus_message_iter_next(&struct_iter);
        dbus_message_iter_get_basic(&struct_iter, &p->h);
        dbus_message_iter_next(&struct_iter);
        dbus_message_iter_recurse(&struct_iter, &inner_array_iter);
        dbus_message_iter_get_fixed_array(&inner_array_iter, &p->pixmap,
                                          &p->len);
        dbus_message_iter_next(&struct_iter);
        dbus_message_iter_next(&array_iter);
        list_insert(pixmaps, &p->link);
    }
    return pixmaps;
}

sni_t *get_sni(DBusConnection *c, const char *name) {
    sni_t *sni = NULL;

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *msg = NULL, *reply = NULL;
    msg = dbus_message_new_method_call(name, ITEM_OBJECT_PATH,
                                       DBUS_INTERFACE_PROPERTIES, "GetAll");
    if (!msg) {
        log_error("Failed to create GetAll message");
        return NULL;
    }

    const char* ifname = ITEM_INTERFACE_NAME;
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &ifname,
                                  DBUS_TYPE_INVALID)) {
        log_error("Failed to append interface name");
        goto cleanup;
    }

    reply = dbus_connection_send_with_reply_and_block(c, msg, -1, &err);
    if (dbus_error_is_set(&err)) {
        log_error("Method call error: %s", err.message);
        goto cleanup;
    }

    // extract sni from reply
    sni = malloc(sizeof(sni_t));
    if (!sni)
        goto cleanup;
    memset(sni, 0, sizeof(*sni));

    DBusMessageIter iter, dict_iter, entry_iter, variant_iter;
    dbus_message_iter_init(reply, &iter);
    dbus_message_iter_recurse(&iter, &dict_iter);

    while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY) {
        dbus_message_iter_recurse(&dict_iter, &entry_iter);

        // 属性名
        const char *key;
        dbus_message_iter_get_basic(&entry_iter, &key);
        dbus_message_iter_next(&entry_iter);

        // 属性值（变体）
        dbus_message_iter_recurse(&entry_iter, &variant_iter);

        // 根据类型输出
        int type = dbus_message_iter_get_arg_type(&variant_iter);
        if (type == DBUS_TYPE_STRING) {
            const char *val;
            dbus_message_iter_get_basic(&variant_iter, &val);
            if (strcmp(key, "Category") == 0) {
                sni->category = strdup(val);
            } else if (strcmp(key, "Id") == 0) {
                sni->id = strdup(val);
            } else if (strcmp(key, "Title") == 0) {
                sni->title = strdup(val);
            } else if (strcmp(key, "Status") == 0) {
                sni->status = strdup(val);
            } else if (strcmp(key, "IconName") == 0) {
                sni->icon.name = strdup(val);
            } else if (strcmp(key, "OverlayIconName") == 0) {
                sni->overlay_icon.name = strdup(val);
            } else if (strcmp(key, "AttentionIconName") == 0) {
                sni->attention_icon.name = strdup(val);
            } else if (strcmp(key, "AttentionMovieName") == 0) {
                sni->attention_movie_name = strdup(val);
            } else if (strcmp(key, "Menu") == 0) {
                sni->menu = strdup(val);
            }
        } else if (type == DBUS_TYPE_UINT32) {
            int val;
            dbus_message_iter_get_basic(&variant_iter, &val);
            if (strcmp(key, "WindowId") == 0) {
                sni->window_id = val;
            }
        } else if (type == DBUS_TYPE_BOOLEAN) {
            dbus_bool_t val;
            dbus_message_iter_get_basic(&variant_iter, &val);
            if (strcmp(key, "ItemIsMenu") == 0) {
                sni->item_is_menu = val;
            }
        } else if (type == DBUS_TYPE_ARRAY) {
            if (strcmp(key, "IconPixmap") == 0) {
                sni->icon.pixmaps = get_icon(&variant_iter);
            } else if (strcmp(key, "OverlayIconPixmap") == 0) {
                sni->overlay_icon.pixmaps = get_icon(&variant_iter);
            } else if (strcmp(key, "AttentionIconPixmap") == 0) {
                sni->attention_icon.pixmaps = get_icon(&variant_iter);
            }
        } else if (type == DBUS_TYPE_STRUCT) {
            if (strcmp(key, "ToolTip") == 0) {
                DBusMessageIter struct_iter, inner_array_iter;
                dbus_message_iter_recurse(&variant_iter, &struct_iter);
                dbus_message_iter_get_basic(&struct_iter,
                                            &sni->tooltip.icon.name);
                dbus_message_iter_next(&struct_iter);
                sni->tooltip.icon.pixmaps = get_icon(&struct_iter);
                dbus_message_iter_next(&struct_iter);
                dbus_message_iter_get_basic(&struct_iter, &sni->tooltip.title);
                dbus_message_iter_next(&struct_iter);
                dbus_message_iter_get_basic(&struct_iter,
                                            &sni->tooltip.description);
                dbus_message_iter_next(&struct_iter);
            }
        }

        dbus_message_iter_next(&dict_iter);
    }

cleanup:
    if (!reply)
        dbus_message_unref(reply);
    if (!msg)
        dbus_message_unref(msg);
    dbus_error_free(&err);
    return sni;
}
