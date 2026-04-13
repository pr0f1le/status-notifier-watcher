#ifndef STATUS_NOTIFIER_H
#define STATUS_NOTIFIER_H

#include <dbus/dbus.h>
#include <stdint.h>

#define HASH_MODULOR 101
#define TABLE_LEN 105
#include "inc/ht.h"
#include "inc/list.h"

#define WATCHER_SERVICE_NAME "org.kde.StatusNotifierWatcher"
#define WATCHER_INTERFACE_NAME "org.kde.StatusNotifierWatcher"
#define HOST_SERVICE_NAME_FMT "org.kde.StatusNotifierHost-%d"
#define WATCHER_OBJECT_PATH "/StatusNotifierWatcher"
#define ITEM_INTERFACE_NAME "org.kde.StatusNotifierItem"
#define ITEM_OBJECT_PATH "/StatusNotifierItem"

#define COLOR_RED "\x1b[31m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_CYAN "\x1b[36m"
#define COLOR_RESET "\x1b[0m"

#define DEC_LOG_FUNC(type) void log_##type(const char *fmt, ...);

DEC_LOG_FUNC(info)
DEC_LOG_FUNC(debug)
DEC_LOG_FUNC(warn)
DEC_LOG_FUNC(error)

typedef struct {
    const uint32_t *pixmap;
    int w, h, len;
    struct list_t link;
} pixmap_t;

typedef struct {
    const char *name;
    struct list_t *pixmaps; // different sizes
} icon_t;

typedef struct {
    const char *category;
    const char *id;
    const char *title;
    const char *status;
    uint32_t window_id;
    icon_t icon;
    icon_t overlay_icon;
    icon_t attention_icon;
    const char *attention_movie_name;
    struct {
        icon_t icon;
        const char *title;
        const char *description;
    } tooltip;
    dbus_bool_t item_is_menu;
    const char* menu;
} sni_t;

typedef void (*sni_handler)(sni_t *);

typedef struct {
    const char *s;
    struct list_t link;
} str_elm;

extern struct dict_t sni_name_to_obj[TABLE_LEN];
extern struct list_t snis;
extern struct list_t snhs;

struct list_t *get_icon(DBusMessageIter *iter);
sni_t *get_sni(DBusConnection *c, const char *name);

#endif // STATUS_NOTIFIER_H
