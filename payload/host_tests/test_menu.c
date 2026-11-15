#include <stdio.h>
#include <string.h>

#include "core/menu.h"

static int failures;
#define CHECK(cond) \
    do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const u16 HAI[] = {0x306F, 0x3044};           /* はい */
static const u16 IIE[] = {0x3044, 0x3044, 0x3048};   /* いいえ */
static const u16 Q[] = {0x3044, 0x3044, 0xFF1F};     /* いい？ */

static struct jpr_ident ident(u32 dat, u32 line) {
    struct jpr_ident id = {41, dat, line, 0, 0, 0};
    return id;
}

static void yes_no(struct jpr_menu *m, const void *object) {
    struct jpr_ident a = ident(10, 0), b = ident(10, 1);
    jpr_menu_add(m, object, JPR_MENU_LIST, &a, HAI, 2);
    jpr_menu_add(m, object, JPR_MENU_LIST, &b, IIE, 3);
}

static void test_a_menu_is_shown_with_its_items_and_their_origins(void) {
    struct jpr_menu m;
    int object = 0;
    jpr_menu_init(&m);
    yes_no(&m, &object);
    CHECK(!m.showing && m.seq == 0);                     /* added, not shown yet */
    jpr_menu_show(&m, &object, 7);
    CHECK(m.showing && m.seq == 1 && m.msg_seq == 7 && m.shown.count == 2 && m.shown.kind == JPR_MENU_LIST);
    CHECK(m.shown.items[0].len == 2 && !memcmp(m.shown.items[0].text, HAI, 4));
    CHECK(m.shown.items[1].id.dat == 10 && m.shown.items[1].id.line == 1);
}

static void test_choosing_and_destroying_hide_it(void) {
    struct jpr_menu m;
    int object = 0, other = 0;
    jpr_menu_init(&m);
    yes_no(&m, &object);
    jpr_menu_show(&m, &object, 7);
    jpr_menu_state(&m, &other, 5);                       /* someone else's menu: nothing */
    CHECK(m.showing);
    jpr_menu_state(&m, &object, 2);
    CHECK(m.showing);
    jpr_menu_state(&m, &object, 4);                      /* chosen */
    CHECK(!m.showing && m.shown.object == &object);
    jpr_menu_gone(&m, &object);
    CHECK(!m.showing && m.shown.object == 0);
    yes_no(&m, &object);                                 /* the same memory, a new menu */
    jpr_menu_show(&m, &object, 9);
    CHECK(m.showing && m.seq == 2 && m.shown.count == 2);
    jpr_menu_gone(&m, &object);
    CHECK(!m.showing);
}

static void test_a_new_object_starts_a_new_menu_and_long_ones_are_cut(void) {
    struct jpr_menu m;
    int a = 0, b = 0;
    u32 k;
    struct jpr_ident id = ident(5, 0);
    u16 long_text[60];
    jpr_menu_init(&m);
    yes_no(&m, &a);
    for (k = 0; k < 60; k++) long_text[k] = 0x3042;
    jpr_menu_add(&m, &b, JPR_MENU_WINDOW, &id, long_text, 60);   /* another menu: a's items are dropped */
    jpr_menu_show(&m, &b, 3);
    CHECK(m.shown.count == 1 && m.shown.kind == JPR_MENU_WINDOW && m.shown.items[0].len == JPR_MENU_TEXT);
    jpr_menu_show(&m, &a, 4);                            /* nothing was added to a since: not shown */
    CHECK(m.shown.object == &b);
    for (k = 0; k < JPR_MENU_ITEMS + 3; k++) jpr_menu_add(&m, &a, JPR_MENU_LIST, &id, HAI, 2);
    jpr_menu_show(&m, &a, 4);
    CHECK(m.shown.count == JPR_MENU_ITEMS && m.shown.more == 1);
    jpr_menu_gone(&m, &b);                               /* not the one showing: nothing */
    CHECK(m.showing);
}

static void test_compose_puts_each_option_on_its_own_line(void) {
    struct jpr_menu m;
    int object = 0;
    u16 out[32], starts[JPR_MENU_ITEMS];
    u32 n;
    jpr_menu_init(&m);
    yes_no(&m, &object);
    jpr_menu_show(&m, &object, 7);
    n = jpr_menu_compose(&m, Q, 3, out, 32, starts);
    /* いい？ \n ・はい \n ・いいえ */
    CHECK(n == 3 + 1 + 1 + 2 + 1 + 1 + 3);
    CHECK(!memcmp(out, Q, 6) && out[3] == 0x000A && out[4] == JPR_MENU_BULLET && starts[0] == 5);
    CHECK(!memcmp(out + 5, HAI, 4) && out[7] == 0x000A && out[8] == JPR_MENU_BULLET && starts[1] == 9);
    CHECK(!memcmp(out + 9, IIE, 6));
    n = jpr_menu_compose(&m, Q, 3, out, 9, starts);      /* room for the first option only */
    CHECK(n == 7 && starts[0] == 5 && starts[1] == 0xFFFF);
}

int main(void) {
    test_a_menu_is_shown_with_its_items_and_their_origins();
    test_choosing_and_destroying_hide_it();
    test_a_new_object_starts_a_new_menu_and_long_ones_are_cut();
    test_compose_puts_each_option_on_its_own_line();
    if (failures) {
        printf("menu: %d check(s) failed\n", failures);
        return 1;
    }
    printf("menu: all checks passed\n");
    return 0;
}
