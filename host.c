#include "host.h"
#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static DBusConnection *conn = NULL;
static const char *host_name;
static void (*do_when_get_sni)(sni_t *);

/*
****************************
*           Host           *
****************************
*/

void get_registered_sni_services();
static dbus_bool_t request_host_name();
static dbus_bool_t register_host_to_watcher();
static dbus_bool_t register_host_signal_handler();

void setup_host(DBusConnection *c) {
    conn = c;
    do_when_get_sni = NULL;

    // 只需要提供一个名字
    if (!request_host_name())
        return;
}

// 独立host
void setup_host_independent(DBusConnection *c) {
    conn = c;
    do_when_get_sni = NULL;

    list_init(&snis);
    memset(sni_name_to_obj, 0, sizeof(struct dict_t) * TABLE_LEN);

    if (!request_host_name())
        return;
    if (!register_host_to_watcher())
        return;
    if (!register_host_signal_handler())
        return;
    get_registered_sni_services();
}

// 获取已注册的所有 Item
// 在host独立运行时使用
void get_registered_sni_services() {
    DBusMessage *msg = NULL;
    DBusMessage *reply = NULL;
    DBusError error;

    dbus_error_init(&error);

    // 调用 Properties.GetAll 获取 RegisteredStatusNotifierItems
    msg =
        dbus_message_new_method_call(WATCHER_SERVICE_NAME, WATCHER_OBJECT_PATH,
                                     DBUS_INTERFACE_PROPERTIES, "Get");
    if (!msg) {
        log_error("Failed to create Get call");
        return;
    }

    const char *interface = WATCHER_INTERFACE_NAME;
    const char *property = "RegisteredStatusNotifierItems";
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &interface,
                                  DBUS_TYPE_STRING, &property,
                                  DBUS_TYPE_INVALID)) {
        log_error("Failed to append args");
        dbus_message_unref(msg);
        return;
    }

    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &error);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&error)) {
        log_error("Failed to get items: %s", error.message);
        dbus_error_free(&error);
        return;
    }

    DBusMessageIter iter, variant_iter;
    dbus_message_iter_init(reply, &iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_VARIANT) {
        log_error("Expected variant, got %d",
                  dbus_message_iter_get_arg_type(&iter));
        goto cleanup;
    }

    dbus_message_iter_recurse(&iter, &variant_iter);

    // 解析字符串数组 (as)
    if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_ARRAY) {
        log_error("Expected array, got %c",
                  dbus_message_iter_get_arg_type(&variant_iter));
        goto cleanup;
    }

    DBusMessageIter array_iter;
    dbus_message_iter_recurse(&variant_iter, &array_iter);

    int item_count = 0;

    // 清空
    str_elm *s, *t;
    list_for_each_safe(s, t, &snis, link) {
        list_remove(&s->link);
        free(s);
    }
    for (int i = 0; i < TABLE_LEN; ++i) {
        sni_name_to_obj[i].str = NULL;
        free(sni_name_to_obj[i].val);
        sni_name_to_obj[i].val = NULL;
    }
    // 重新获取
    while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
        ++item_count;
        str_elm *s = calloc(1, sizeof(str_elm));
        char *v;
        dbus_message_iter_get_basic(&array_iter, &v);
        s->s = strdup(v);
        list_insert(&snis, &s->link);

        sni_t *sni = get_sni(conn, v);
        ht_add(sni_name_to_obj, strdup(v), strlen(v), sni);

        log_debug("Host get item service name: %s", v);
        dbus_message_iter_next(&array_iter);
    }
    log_debug("Item count: %d", item_count);

cleanup:
    dbus_message_unref(reply);
}

// 监听信号
static DBusHandlerResult signal_handler(DBusConnection *c, DBusMessage *msg,
                                        void *user_data) {
    const char *interface = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);

    if (strcmp(interface, WATCHER_INTERFACE_NAME) == 0) {
        if (strcmp(member, "StatusNotifierItemRegistered") == 0) {
            const char *service;
            if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &service,
                                      DBUS_TYPE_INVALID)) {
                log_debug("New item registered: %s", service);
            }
        } else if (strcmp(member, "StatusNotifierItemUnregistered") == 0) {
            const char *service;
            if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &service,
                                      DBUS_TYPE_INVALID)) {
                log_debug("Item unregistered: %s ", service);
            }
        } else if (strcmp(member, "StatusNotifierHostRegistered") == 0) {
            log_debug("Another host registered");
        }
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static dbus_bool_t request_host_name() {
    DBusError err;
    dbus_error_init(&err);

    // 构造 host_name
    char tmp[256];
    sprintf(tmp, HOST_SERVICE_NAME_FMT, getpid());
    host_name = strdup(tmp);

    // 获取名称
    int ret;
    ret = dbus_bus_request_name(conn, host_name,
                                DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (dbus_error_is_set(&err)) {
        log_error("DBus request name (%s) error: %s", host_name, err.message);
        dbus_error_free(&err);
        return FALSE;
    }

    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        log_error("Failed to acquire name: %s", host_name);
        return FALSE;
    }
    log_debug("Acquired name: %s", host_name);
    return TRUE;
}

static dbus_bool_t register_host_to_watcher() {
    DBusMessage *m, *reply;
    DBusError err;
    dbus_error_init(&err);

    dbus_bool_t success = FALSE;

    m = dbus_message_new_method_call(WATCHER_SERVICE_NAME, WATCHER_OBJECT_PATH,
                                     WATCHER_INTERFACE_NAME,
                                     "RegisterStatusNotifierHost");
    if (!m) {
        log_error("Failed to creat method call");
        goto cleanup;
    }

    // 向 Watcher 注册 Host
    const char *host_name_str = host_name;
    if (!dbus_message_append_args(m, DBUS_TYPE_STRING, &host_name_str,
                                  DBUS_TYPE_INVALID)) {
        log_error("Failed to append args");
        goto cleanup;
    }

    reply = dbus_connection_send_with_reply_and_block(conn, m, -1, &err);

    if (dbus_error_is_set(&err)) {
        log_error("Failed to register host to watcher");
        goto cleanup;
    }

    success = TRUE;
    log_debug("Register to watcher success");

cleanup:
    if (m)
        dbus_message_unref(m);
    if (reply)
        dbus_message_unref(reply);

    return success;
}

// 添加信号匹配规则
static dbus_bool_t register_host_signal_handler() {
    DBusError error;
    dbus_error_init(&error);

    // 匹配 StatusNotifierItemRegistered 信号
    dbus_bus_add_match(conn,
                       "type='signal',"
                       "interface='" WATCHER_INTERFACE_NAME "',"
                       "member='StatusNotifierItemRegistered'",
                       &error);
    if (dbus_error_is_set(&error)) {
        log_warn("Failed to add match: %s", error.message);
        dbus_error_free(&error);
        return FALSE;
    }

    // 匹配 StatusNotifierItemUnregistered 信号
    dbus_bus_add_match(conn,
                       "type='signal',"
                       "interface='" WATCHER_INTERFACE_NAME "',"
                       "member='StatusNotifierItemUnregistered'",
                       &error);
    if (dbus_error_is_set(&error)) {
        log_warn("Failed to add match: %s", error.message);
        dbus_error_free(&error);
        return FALSE;
    }

    // 匹配 StatusNotifierHostRegistered 信号
    dbus_bus_add_match(conn,
                       "type='signal',"
                       "interface='" WATCHER_INTERFACE_NAME "',"
                       "member='StatusNotifierHostRegistered'",
                       &error);
    if (dbus_error_is_set(&error)) {
        log_warn("Failed to add match: %s", error.message);
        dbus_error_free(&error);
        return FALSE;
    }
    dbus_error_free(&error);

    if (!dbus_connection_add_filter(conn, signal_handler, NULL, NULL)) {
        log_warn("Failed to add filter");
        return FALSE;
    }
    log_debug("Host signal handler registered");

    return TRUE;
}

void register_sni_handler(sni_handler sc) { do_when_get_sni = sc; }

void handle_snis() {
    if (!do_when_get_sni) return;
    str_elm *s;
    list_for_each(s, &snis, link) {
        sni_t *sni = ht_get(sni_name_to_obj, s->s, strlen(s->s));
        do_when_get_sni(sni);
    }
}
