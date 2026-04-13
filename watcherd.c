#include "watcherd.h"
#include "dbus/dbus-protocol.h"
#include "shared.h"

#include <stdio.h>
#include <dbus/dbus.h>
#include <stdlib.h>
#include <string.h>

static DBusConnection *conn = NULL;
static int host_registered = 0; // number of registered hosts
static int protocol_version = 0;

/*
*****************************
*          Watcher          *
*****************************
*/

static dbus_bool_t request_watcher_name();
static dbus_bool_t register_object();
static DBusHandlerResult message_handler(DBusConnection *c, DBusMessage *m,
                                         void *user_data);
static DBusHandlerResult signal_handler(DBusConnection *c, DBusMessage *m,
                                        void *user_data);
static dbus_bool_t register_watcherd_signal_handler();

void setup_watcherd(DBusConnection *c) {
    conn = c;

    list_init(&snis);
    list_init(&snhs);
    memset(sni_name_to_obj, 0, sizeof(struct dict_t) * TABLE_LEN);

    if (!request_watcher_name())
        return;
    if (!register_object())
        return;
    if (!register_watcherd_signal_handler())
        return;
    host_registered = 1;
}

// 独立的watcherd
void setup_watcherd_independent(DBusConnection *c) {
    conn = c;

    list_init(&snis);
    list_init(&snhs);
    memset(sni_name_to_obj, 0, sizeof(struct dict_t) * TABLE_LEN);

    if (!request_watcher_name())
        return;
    if (!register_object())
        return;
    if (!register_watcherd_signal_handler())
        return;
}

static dbus_bool_t request_watcher_name() {
    DBusError err;
    dbus_error_init(&err);

    int ret = dbus_bus_request_name(conn, WATCHER_SERVICE_NAME,
                                    DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (dbus_error_is_set(&err)) {
        log_error("DBus request name (" WATCHER_SERVICE_NAME ") error: %s",
                  err.message);
        dbus_error_free(&err);
        return FALSE;
    }

    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        log_error("Failed to acquire name: %s", WATCHER_SERVICE_NAME);
        return FALSE;
    }
    log_debug("Acquired name: %s", WATCHER_SERVICE_NAME);
    return TRUE;
}

static dbus_bool_t register_object() {
    DBusError err;
    dbus_error_init(&err);

    DBusObjectPathVTable vtable = {.message_function = message_handler,
                                   .unregister_function = NULL};
    if (!dbus_connection_register_object_path(conn, WATCHER_OBJECT_PATH,
                                              &vtable, NULL)) {
        log_error("Failed to register object path: %s", WATCHER_OBJECT_PATH);
        return FALSE;
    }
    log_debug("Object registered at %s", WATCHER_OBJECT_PATH);
    return TRUE;
}

// ========== Signal ==========
// 发送 StatusNotifierItemRegistered 信号
static void send_sni_registered_signal(const char *service_name) {
    DBusMessage *signal;
    DBusMessageIter iter;

    // 创建信号消息
    signal =
        dbus_message_new_signal(WATCHER_OBJECT_PATH, WATCHER_INTERFACE_NAME,
                                "StatusNotifierItemRegistered");
    if (!signal) {
        log_warn("Failed to create signal");
        return;
    }

    if (!dbus_message_append_args(signal, DBUS_TYPE_STRING, &service_name,
                                  DBUS_TYPE_INVALID)) {
        log_warn("Failed to append service name to args");
        dbus_message_unref(signal);
        return;
    }

    // 发送信号
    dbus_connection_send(conn, signal, NULL);
    dbus_connection_flush(conn);
    dbus_message_unref(signal);

    log_debug("Signal sent: StatusNotifierItemRegistered (%s)", service_name);
}

// 发送 StatusNotifierItemUnregistered 信号
static void send_sni_unregistered_signal(const char *service_name) {
    DBusMessage *signal;
    DBusMessageIter iter;

    signal =
        dbus_message_new_signal(WATCHER_OBJECT_PATH, WATCHER_INTERFACE_NAME,
                                "StatusNotifierItemUnregistered");
    if (!signal) {
        log_warn("Failed to create signal");
        return;
    }

    if (!dbus_message_append_args(signal, DBUS_TYPE_STRING, &service_name,
                                  DBUS_TYPE_INVALID)) {
        log_warn("Failed to append service name to args");
        dbus_message_unref(signal);
        return;
    }

    dbus_connection_send(conn, signal, NULL);
    dbus_connection_flush(conn);
    dbus_message_unref(signal);

    log_debug("Signal sent: StatusNotifierItemUnregistered (%s)", service_name);
}

// 发送 StatusNotifierHostRegistered 信号（无参数）
static void send_host_registered_signal(void) {
    DBusMessage *signal;

    signal =
        dbus_message_new_signal(WATCHER_OBJECT_PATH, WATCHER_INTERFACE_NAME,
                                "StatusNotifierHostRegistered");
    if (!signal) {
        log_warn("Failed to create signal");
        return;
    }

    dbus_connection_send(conn, signal, NULL);
    dbus_connection_flush(conn);
    dbus_message_unref(signal);

    log_debug("Signal sent: StatusNotifierHostRegistered");
}

// ========== Method ==========
static DBusMessage *handle_property_set(DBusMessage *m) {
    return dbus_message_new_error(m, DBUS_ERROR_PROPERTY_READ_ONLY,
                                  "Property is read-only");
}

static int append_variant_string_array(DBusMessageIter *iter,
                                       struct list_t *lst) {
    DBusMessageIter variant_iter, array_iter;
    int success = 0;
    // open variant as 'as'
    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "as",
                                          &variant_iter))
        return -1;
    // open array
    if (!dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_ARRAY,
                                          DBUS_TYPE_STRING_AS_STRING,
                                          &array_iter))
        return -1;

    int here_success = 0;
    str_elm *s;
    list_for_each(s, lst, link) {
        here_success = 0;
        if (!dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING,
                                            &s->s)) {
            return -1;
        }
        here_success = 1;
    }
    success = here_success;

    // close array
    if (!dbus_message_iter_close_container(&variant_iter, &array_iter))
        return -1;
    // close
    if (!dbus_message_iter_close_container(iter, &variant_iter))
        return -1;

    return success;
}

static int append_variant_int(DBusMessageIter *iter, int v) {
    DBusMessageIter variant_iter;
    if (!dbus_message_iter_open_container(
            iter, DBUS_TYPE_VARIANT, DBUS_TYPE_INT32_AS_STRING, &variant_iter))
        return -1;

    if (!dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_INT32, &v))
        return -1;

    if (!dbus_message_iter_close_container(iter, &variant_iter))
        return -1;

    return 1;
}

static int append_variant_boolean(DBusMessageIter *iter, dbus_bool_t v) {
    DBusMessageIter variant_iter;
    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
                                          DBUS_TYPE_BOOLEAN_AS_STRING,
                                          &variant_iter))
        return -1;

    if (!dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_BOOLEAN, &v))
        return -1;

    if (!dbus_message_iter_close_container(iter, &variant_iter))
        return -1;

    return 1;
}

static int append_variant_string(DBusMessageIter *iter, const char *str) {
    DBusMessageIter variant_iter;
    if (!dbus_message_iter_open_container(
            iter, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &variant_iter))
        return -1;

    if (!dbus_message_iter_append_basic(&variant_iter, DBUS_TYPE_STRING, &str))
        return -1;

    if (!dbus_message_iter_close_container(iter, &variant_iter))
        return -1;

    return 1;
}

static DBusMessage *handle_property_get(DBusMessage *m) {
    DBusMessage *reply;
    DBusMessageIter iter;
    const char *interface, *property;

    DBusError err;
    dbus_error_init(&err);

    if (!dbus_message_get_args(m, &err, DBUS_TYPE_STRING, &interface,
                               DBUS_TYPE_STRING, &property,
                               DBUS_TYPE_INVALID)) {
        log_warn("Invalid args: %s", err.message);
        dbus_error_free(&err);
        return dbus_message_new_error(m, DBUS_ERROR_INVALID_ARGS,
                                      "Invalid args");
    }
    dbus_error_free(&err);
    log_debug("Method Get called with interface %s and property %s", interface,
              property);

    // 只能查询 StatusNotifierWatcher 接口下的 property
    if (strcmp(interface, WATCHER_INTERFACE_NAME) != 0) {
        log_warn("Client tried to get property %s from unknown interface: %s",
                 property, interface);
        return dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_INTERFACE,
                                      "Unknown interface");
    }

    reply = dbus_message_new_method_return(m);
    if (!reply) {
        log_warn("Construct reply failed");
        return dbus_message_new_error(m, DBUS_ERROR_FAILED,
                                      "Failed to construct reply");
    }

    dbus_message_iter_init_append(reply, &iter);

    int ret = -1;
    if (strcmp(property, "RegisteredStatusNotifierItems") == 0) {
        ret = append_variant_string_array(&iter, &snis);
    } else if (strcmp(property, "IsStatusNotifierHostRegistered") == 0) {
        ret = append_variant_boolean(&iter, (dbus_bool_t)(host_registered > 0));
    } else if (strcmp(property, "ProtocolVersion") == 0) {
        ret = append_variant_int(&iter, protocol_version);
    }

    if (ret < 0) {
        dbus_message_unref(reply);
        return dbus_message_new_error(m, DBUS_ERROR_FAILED,
                                      "Failed to get property");
    }

    if (ret == 0) {
        log_warn("Can not get property, maybe array empty: %s", property);
    }

    return reply;
}

static DBusMessage *handle_property_getall(DBusMessage *m) {
    DBusMessage *reply;
    DBusMessageIter iter, dict_iter, variant_iter;
    const char *interface;

    DBusError err;
    dbus_error_init(&err);

    if (!dbus_message_get_args(m, &err, DBUS_TYPE_STRING, &interface,
                               DBUS_TYPE_INVALID)) {
        return dbus_message_new_error(m, DBUS_ERROR_INVALID_ARGS,
                                      "Invalid args");
    }
    log_debug("Method GetAll called with interface %s", interface);

    if (strcmp(interface, WATCHER_INTERFACE_NAME) != 0) {
        log_warn("Client tried to get unknown interface %s", interface);
        return dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_INTERFACE,
                                      "Unknown interface");
    }

    reply = dbus_message_new_method_return(m);
    if (!reply) {
        log_warn("Construct reply failed");
        return NULL;
    }

    dbus_message_iter_init_append(reply, &iter);

    if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}",
                                          &dict_iter)) {
        log_warn("Open container dict failed");
        dbus_message_unref(reply);
        return NULL;
    }

    DBusMessageIter dict_entry_iter;

    { // snis
        const char *key = "RegisteredStatusNotifierItems";
        int ret;
        if (dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY,
                                             NULL, &dict_entry_iter)) {
            dbus_message_iter_append_basic(&dict_entry_iter, DBUS_TYPE_STRING,
                                           &key);
            ret = append_variant_string_array(&dict_entry_iter, &snis);
            dbus_message_iter_close_container(&dict_iter, &dict_entry_iter);
        }
        if (ret < 0) {
            dbus_message_unref(reply);
            return NULL;
        }
    }

    { // is host registered
        const char *key = "IsStatusNotifierHostRegistered";
        int ret;
        if (dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY,
                                             NULL, &dict_entry_iter)) {
            dbus_message_iter_append_basic(&dict_entry_iter, DBUS_TYPE_STRING,
                                           &key);
            ret = append_variant_boolean(&dict_entry_iter,
                                         (dbus_bool_t)(host_registered > 0));
            dbus_message_iter_close_container(&dict_iter, &dict_entry_iter);
        }
        if (ret < 0) {
            dbus_message_unref(reply);
            return NULL;
        }
    }

    { // protocol version
        const char *key = "ProtocolVersion";
        int ret;
        if (dbus_message_iter_open_container(&dict_iter, DBUS_TYPE_DICT_ENTRY,
                                             NULL, &dict_entry_iter)) {
            dbus_message_iter_append_basic(&dict_entry_iter, DBUS_TYPE_STRING,
                                           &key);
            ret = append_variant_int(&dict_entry_iter, protocol_version);
            dbus_message_iter_close_container(&dict_iter, &dict_entry_iter);
        }
        if (ret < 0) {
            dbus_message_unref(reply);
            return NULL;
        }
    }

    if (!dbus_message_iter_close_container(&iter, &dict_iter)) {
        dbus_message_unref(reply);
        return NULL;
    }

    return reply;
}

#include "introspect.xml.h"
static DBusMessage *handle_introspect(DBusMessage *m) {
    DBusMessage *reply = dbus_message_new_method_return(m);
    const char *xml = (char *)introspect_xml;
    if (reply) {
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml);
    }
    return reply;
}

// 处理 StatusNotifierWatcher.RegisterStatusNotifierItem
static DBusMessage *handle_rsni(DBusMessage *m) {
    const char *service;

    DBusError err;
    dbus_error_init(&err);

    if (!dbus_message_get_args(m, &err, DBUS_TYPE_STRING, &service)) {
        log_warn("Invalid args: %s", err.message);
        dbus_error_free(&err);
        return dbus_message_new_error(m, DBUS_ERROR_INVALID_ARGS,
                                      "Invalide args");
    }
    dbus_error_free(&err);
    log_debug("Item registered service: %s", service);

    str_elm *s = calloc(1, sizeof(str_elm));
    s->s = strdup(service);
    list_insert(&snis, &s->link);

    sni_t *sni = get_sni(conn, service);
    ht_add(sni_name_to_obj, strdup(service), strlen(service), sni);

    send_sni_registered_signal(service);

    return dbus_message_new_method_return(m);
}

// 处理 StatusNotifierWatcher.RegisterStatusNotifierHost
static DBusMessage *handle_rsnh(DBusMessage *m) {
    const char *service;

    DBusError err;
    dbus_error_init(&err);

    if (!dbus_message_get_args(m, &err, DBUS_TYPE_STRING, &service)) {
        log_warn("Invalid args: %s", err.message);
        dbus_error_free(&err);
        return dbus_message_new_error(m, DBUS_ERROR_INVALID_ARGS,
                                      "Invalide args");
    }
    dbus_error_free(&err);
    log_debug("Host registered service: %s", service);

    str_elm *s = calloc(1, sizeof(str_elm));
    s->s = strdup(service);
    list_insert(&snhs, &s->link);
    host_registered += 1;

    send_host_registered_signal();

    return dbus_message_new_method_return(m);
}

static DBusMessage *handle_ping(DBusMessage *m) {
    return dbus_message_new_method_return(m);
}

static DBusMessage *handle_get_machine_id(DBusMessage *m) {
    DBusMessage *reply = dbus_message_new_method_return(m);
    if (!reply) return NULL;
    
    // 读取 /etc/machine-id
    FILE *f = fopen("/etc/machine-id", "r");
    if (!f) {
        const char *fallback = "00000000000000000000000000000000";
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &fallback,
                                 DBUS_TYPE_INVALID);
        return reply;
    }
    
    char machine_id[64];
    if (fgets(machine_id, sizeof(machine_id), f)) {
        size_t len = strlen(machine_id);
        if (len > 0 && machine_id[len-1] == '\n')
            machine_id[len-1] = '\0';
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &machine_id,
                                 DBUS_TYPE_INVALID);
    } else {
        const char *fallback = "00000000000000000000000000000000";
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &fallback,
                                 DBUS_TYPE_INVALID);
    }
    fclose(f);
    return reply;
}

// ========== Handler ==========
static DBusHandlerResult message_handler(DBusConnection *c, DBusMessage *m,
                                         void *user_data) {
    DBusMessage *reply = NULL;
    const char *method;
    const char *interface;

    method = dbus_message_get_member(m);
    interface = dbus_message_get_interface(m);

    log_debug("Received method %s on interface %s", method ? method : "NULL",
              interface ? interface : "NULL");

    if (strcmp(interface, DBUS_INTERFACE_PROPERTIES) == 0) {
        if (strcmp(method, "Get") == 0) {
            reply = handle_property_get(m);
        } else if (strcmp(method, "Set") == 0) {
            reply = handle_property_set(m);
        } else if (strcmp(method, "GetAll") == 0) {
            reply = handle_property_getall(m);
        } else {
            reply = dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_METHOD,
                                           "Unknown method");
        }
    } else if (strcmp(interface, WATCHER_INTERFACE_NAME) == 0) {
        if (strcmp(method, "RegisterStatusNotifierItem") == 0) {
            reply = handle_rsni(m);
        } else if (strcmp(method, "RegisterStatusNotifierHost") == 0) {
            reply = handle_rsnh(m);
        } else {
            reply = dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_METHOD,
                                           "Unknown method");
        }
    } else if (strcmp(interface, DBUS_INTERFACE_INTROSPECTABLE) == 0 &&
               strcmp(method, "Introspect") == 0) {
        reply = handle_introspect(m);
    } else if (strcmp(interface, DBUS_INTERFACE_PEER) == 0) {
        if (strcmp(method, "Ping") == 0) {
            reply = handle_ping(m);
        } else if (strcmp(method, "GetMachineId") == 0) {
            reply = handle_get_machine_id(m);
        } else {
            reply = dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
        }
    }
    else {
        reply = dbus_message_new_error(m, DBUS_ERROR_UNKNOWN_INTERFACE,
                                       "Unknown interface");
    }

    if (reply) {
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult signal_handler(DBusConnection *c, DBusMessage *msg,
                                        void *user_data) {
    const char *interface = dbus_message_get_interface(msg);
    const char *member = dbus_message_get_member(msg);

    if (strcmp(interface, "org.freedesktop.DBus") == 0) {
        if (strcmp(member, "NameOwnerChanged") == 0) {
            const char *name, *old, *new;
            if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &name,
                                      DBUS_TYPE_STRING, &old, DBUS_TYPE_STRING,
                                      &new, DBUS_TYPE_INVALID)) {
                if (new[0] == '\0') {
                    int is_in_list = 0;
                    str_elm *s;
                    list_for_each(s, &snis, link) {
                        if (strcmp(s->s, name) == 0) {
                            sni_t *tmps = ht_get(sni_name_to_obj, s->s, strlen(s->s));
                            free(tmps);
                            ht_del(sni_name_to_obj, s->s, strlen(s->s));
                            list_remove(&s->link);
                            free(s);
                            is_in_list = 1;
                            break;
                        }
                    }
                    if (is_in_list) {
                        log_debug("Name: %s offline", name);
                        send_sni_unregistered_signal(name);
                    }
                }
            }
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static dbus_bool_t register_watcherd_signal_handler() {
    DBusError error;
    dbus_error_init(&error);

    // 匹配 StatusNotifierItemRegistered 信号
    dbus_bus_add_match(conn,
                       "type='signal',"
                       "interface='org.freedesktop.DBus',"
                       "member='NameOwnerChanged'",
                       &error);
    if (dbus_error_is_set(&error)) {
        log_warn("Failed to add match: %s", error.message);
        dbus_error_free(&error);
        return FALSE;
    }

    if (!dbus_connection_add_filter(conn, signal_handler, NULL, NULL)) {
        log_warn("Failed to add filter");
        return FALSE;
    }
    log_debug("Host signal handler registered");

    return TRUE;
}
