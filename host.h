#pragma once

#include <dbus/dbus.h>
#include "shared.h"

void setup_host(DBusConnection *);
void setup_host_independent(DBusConnection *);
void get_registered_sni_services();
void register_sni_handler(sni_handler sc);
void handle_snis();
