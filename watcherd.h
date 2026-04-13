#pragma once

#include <dbus/dbus.h>

void setup_watcherd(DBusConnection *);
void setup_watcherd_independent(DBusConnection *);
