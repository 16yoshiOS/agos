/* =============================================================================
 *  AGOS - CMOS real-time clock (sys/rtc.c)
 * ========================================================================== */
#include "rtc.h"
#include "io.h"
#include "lib.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

#define REG_SEC    0x00
#define REG_MIN    0x02
#define REG_HOUR   0x04
#define REG_DAY    0x07
#define REG_MON    0x08
#define REG_YEAR   0x09
#define REG_STA    0x0A
#define REG_STB    0x0B
#define REG_CENT   0x32

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_INDEX, reg);               /* bit 7 clear: NMI stays enabled      */
    io_wait();
    return inb(CMOS_DATA);
}

static uint8_t bcd(uint8_t v)
{
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

unsigned rtc_day_of_week(unsigned year, unsigned mon, unsigned mday)
{
    static const unsigned t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };

    if (mon < 1 || mon > 12)
        return 0;
    if (mon < 3)
        year--;
    return (year + year / 4 - year / 100 + year / 400 + t[mon - 1] + mday) % 7;
}

struct raw { uint8_t sec, min, hour, day, mon, year, cent; };

static void read_raw(struct raw *r)
{
    for (int spin = 0; spin < 10000 && (cmos_read(REG_STA) & 0x80); spin++)
        ;                                /* wait out an update in progress      */
    r->sec  = cmos_read(REG_SEC);
    r->min  = cmos_read(REG_MIN);
    r->hour = cmos_read(REG_HOUR);
    r->day  = cmos_read(REG_DAY);
    r->mon  = cmos_read(REG_MON);
    r->year = cmos_read(REG_YEAR);
    r->cent = cmos_read(REG_CENT);
}

bool rtc_read(struct rtc_time *t)
{
    struct raw a, b;
    uint8_t stb;
    bool binary, h24, pm;

    /* Two identical consecutive reads guarantee we did not straddle an update. */
    read_raw(&a);
    for (int i = 0; i < 4; i++) {
        read_raw(&b);
        if (memcmp(&a, &b, sizeof(a)) == 0)
            break;
        a = b;
    }

    stb = cmos_read(REG_STB);
    binary = (stb & 0x04) != 0;
    h24 = (stb & 0x02) != 0;
    pm = (a.hour & 0x80) != 0;
    a.hour &= 0x7F;

    if (!binary) {
        a.sec = bcd(a.sec);
        a.min = bcd(a.min);
        a.hour = bcd(a.hour);
        a.day = bcd(a.day);
        a.mon = bcd(a.mon);
        a.year = bcd(a.year);
        a.cent = bcd(a.cent);
    }
    if (!h24) {                          /* 12-hour clock: 12 AM = 0, 12 PM = 12 */
        a.hour %= 12;
        if (pm)
            a.hour += 12;
    }

    t->sec = a.sec;
    t->min = a.min;
    t->hour = a.hour;
    t->mday = a.day;
    t->mon = a.mon;
    t->year = (uint16_t)((a.cent >= 19 && a.cent <= 21 ? a.cent : 20) * 100 + a.year);
    t->wday = (uint8_t)rtc_day_of_week(t->year, t->mon, t->mday);

    return t->sec < 60 && t->min < 60 && t->hour < 24 &&
           t->mon >= 1 && t->mon <= 12 && t->mday >= 1 && t->mday <= 31;
}
