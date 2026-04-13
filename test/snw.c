#include "dbus/dbus-shared.h"
#include "dbus/dbus.h"
#include "../watcherd.h"

int main() {
    DBusError err;
    dbus_error_init(&err);
    DBusConnection *c = dbus_bus_get(DBUS_BUS_SESSION, &err);
    setup_watcherd_independent(c);
    while (dbus_connection_read_write_dispatch(c, -1))
        ;
    return 0;
}
