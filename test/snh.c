#include "../host.h"
#include <dbus/dbus.h>
#include <stdio.h>

void print_snis(sni_t *s) {
    puts("========SNI========");
    printf("Category: %s\n", s->category);
    printf("Id: %s\n", s->category);
    printf("Title: %s\n", s->category);
    printf("Status: %s\n", s->category);
    printf("WindowId: %u\n", s->window_id);
    printf("IconName: %s\n", s->icon.name);
    printf("Icon: %s\n", s->icon.name);
    pixmap_t *p;
    list_for_each(p, s->icon.pixmaps, link) {
        printf("\tSize: %dx%d\n", p->w, p->h);
        printf("\tLen: %d\n", p->len);
    }
    printf("OverlayIconName: %s\n", s->overlay_icon.name);
    list_for_each(p, s->overlay_icon.pixmaps, link) {
        printf("\tSize: %dx%d\n", p->w, p->h);
        printf("\tLen: %d\n", p->len);
    }
    printf("AttentionIconName: %s\n", s->attention_icon.name);
    list_for_each(p, s->attention_icon.pixmaps, link) {
        printf("\tSize: %dx%d\n", p->w, p->h);
        printf("\tLen: %d\n", p->len);
    }
    printf("AttentionMovieName: %s\n", s->attention_movie_name);
    printf("Tooltip: %s\n", s->tooltip.title);
    printf("\t%s\n", s->tooltip.description);
    printf("\tIcon: %s\n", s->tooltip.icon.name);
    list_for_each(p, s->tooltip.icon.pixmaps, link) {
        printf("\t\tSize: %dx%d\n", p->w, p->h);
        printf("\t\tLen: %d\n", p->len);
    }
    printf("ItemIsMenu: %s\n", s->item_is_menu == TRUE ? "TRUE" : "FALSE");
    printf("Menu: %s\n", s->menu);
    puts("========SNI========");
}

int main() {
    DBusError err;
    dbus_error_init(&err);
    DBusConnection *c = dbus_bus_get(DBUS_BUS_SESSION, &err);
    setup_host_independent(c);
    register_sni_handler(print_snis);
    while (dbus_connection_read_write_dispatch(c, -1))
        ;
}
