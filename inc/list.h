#ifndef LIST_H
#define LIST_H

#include <stddef.h>
#include <math.h>

struct list_t {
    struct list_t *prev, *next;
};

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#define typeof(expr) __typeof__(expr)
#endif

#define container_of(ptr, of_type, with_member) \
  (of_type*)((char*)(ptr) - offsetof(of_type, with_member))

#define list_for_each(pos, head, member) \
  for (pos = container_of((head)->next, typeof(*pos), member); \
      &pos->member != (head); \
      pos = container_of(pos->member.next, typeof(*pos), member))

#define list_for_each_safe(pos, tmp, head, member) \
  for (pos = container_of((head)->next, typeof(*pos), member), \
      tmp = container_of((pos)->member.next, typeof(*tmp), member); \
      &pos->member != (head); \
      pos = tmp, \
      tmp = container_of(pos->member.next, typeof(*tmp), member))

void list_init(struct list_t*);
void list_insert(struct list_t* lst, struct list_t* elm);
void list_remove(struct list_t* elm);
#endif // LIST_H

#ifdef LIST_IMPLEMENTATION

void list_init(struct list_t *lst) {
    lst->next = lst;
    lst->prev = lst;
}

/*
a : head

a - b
|   | + x : a->prev = c, c->next = a
- c -
  v       : x->prev = a->prev = c, x->next = a, a->prev = x, a->prev->next = x
a - b
|   |     : a->prev = x, c->next = x
x - c

a + x     : a->prev = a, a->next = a
  v       : x->prev = a->prev = a, x->next = a, a->prev->next = a->next = x, a->prev = x
a - x     : a->prev = x, a->next = x

*/

void list_insert(struct list_t* lst, struct list_t* elm) {
    elm->prev = lst->prev;
    elm->next = lst;
    lst->prev->next = elm;
    lst->prev = elm;
}

void list_remove(struct list_t *elm) {
    elm->prev->next = elm->next;
    elm->next->prev = elm->prev;
}

#endif // LIST_IMPLEMENTATION
