/**
    modules/clock.c: clock module for openchronos-ng

    Copyright (C) 2012 Angelo Arrifano <miknix@gmail.com>
    Copyright (C) 2016 Benjamin Sølberg <benjamin.soelberg@gmail.com>

    http://github.com/BenjaminSoelberg/openchronos-ng-elf

    This file is part of openchronos-ng.

    openchronos-ng is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    openchronos-ng is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#include "messagebus.h"
#include "menu.h"

/* drivers */
#include "drivers/rtca.h"
#include "drivers/display.h"

#ifdef CONFIG_MOD_CLOCK_MONTH_FIRST
    #define MONTH_SEGMENT   (LCD_SEG_L2_4_3)
    #define DAY_SEGMENT     (LCD_SEG_L2_1_0)
#else
    #define MONTH_SEGMENT   (LCD_SEG_L2_1_0)
    #define DAY_SEGMENT     (LCD_SEG_L2_4_3)
#endif

#define SECONDS_SEGMENT (LCD_SEG_L2_1_0)

static uint8_t display_seconds = 0;

static void clock_event(enum sys_message msg)
{
#ifdef CONFIG_MOD_CLOCK_BLINKCOL
    if (msg & SYS_MSG_RTC_SECOND) {
        display_symbol(0, LCD_SEG_L1_COL, ((rtca_time.sec & 0x01) ? SEG_ON : SEG_OFF));
    }
#endif
    if (display_seconds) {
        if (msg & SYS_MSG_RTC_SECOND) {
            _printf(0, SECONDS_SEGMENT, "%02u", rtca_time.sec);
        }
    } else {
        if ((msg & SYS_MSG_RTC_DAY) || (msg & SYS_MSG_RTC_MONTH)) // Collapsed to simplify code path
        {
            _printf(0, MONTH_SEGMENT, "%02u", rtca_time.mon);
            _printf(0, DAY_SEGMENT, "%02u", rtca_time.day);
            display_char (0, LCD_SEG_L2_2, '-', SEG_SET);
        }
    }

    if (msg & SYS_MSG_RTC_DAY)
        _printf(1, LCD_SEG_L2_2_0, rtca_dow_str[rtca_time.dow], SEG_SET);

    if (msg & SYS_MSG_RTC_YEAR)
        _printf(1, LCD_SEG_L1_3_0, "%04u", rtca_time.year);

    if (msg & SYS_MSG_RTC_HOUR) {
        if (display_am_pm) {
            uint8_t tmp_hh = rtca_time.hour;
            if (tmp_hh > 12) { //PM
                tmp_hh -= 12;
                display_symbol(0, LCD_SYMB_PM, SEG_SET);
            } else {
                if (tmp_hh == 12) { // PM
                    display_symbol(0, LCD_SYMB_PM, SEG_SET);
                } else { // AM
                    display_symbol(0, LCD_SYMB_PM, SEG_OFF);
                }
                if (tmp_hh == 0)
                    tmp_hh = 12;
            }
            _printf(0, LCD_SEG_L1_3_2, "%2u", tmp_hh);
        } else {
            _printf(0, LCD_SEG_L1_3_2, "%02u", rtca_time.hour);
            display_symbol(0, LCD_SYMB_PM, SEG_OFF);
        }
    }
    if (msg & SYS_MSG_RTC_MINUTE)
        _printf(0, LCD_SEG_L1_1_0, "%02u", rtca_time.min);
}

/* update screens with fake event */
static inline void update_screen()
{
    clock_event(SYS_MSG_RTC_YEAR | SYS_MSG_RTC_MONTH | SYS_MSG_RTC_DAY |
                SYS_MSG_RTC_HOUR | SYS_MSG_RTC_MINUTE | SYS_MSG_RTC_SECOND);
}

/* In effect when adjusting from one month to another month with less days and the day needs to be adjusted,
   This effect can also be seen when changing from leap year to non leap year and the date is 02-29 */
static void auto_adjust_dd()
{
    uint8_t min_day = rtca_get_max_days(rtca_time.mon, rtca_time.year);
    if (min_day < rtca_time.day) {
        rtca_time.day = min_day;
        update_screen();
    }
}

/********************* edit mode callbacks ********************************/

/* Year */
static void edit_yy_sel(void)
{
    lcd_screen_activate(1);
    display_chars(1, LCD_SEG_L1_3_0, NULL, BLINK_ON);
}

static void edit_yy_dsel(void)
{
    display_chars(1, LCD_SEG_L1_3_0, NULL, BLINK_OFF);
}

static void edit_yy_set(int8_t step)
{
    /* this allows setting years between 2012 and 2047 */
    // 0x07DC = 2012 and 0x07FF = 2047
    // The helpers_loop will only handle the low byte
    *((uint8_t *)&rtca_time.year + 1) = 0x07;
    helpers_loop((uint8_t *)&rtca_time.year, 0xDC, 0xFF, step);

    auto_adjust_dd();
    rtca_update_dow();
    update_screen();
}

/* Month */
static void edit_mo_sel(void)
{
    lcd_screen_activate(0);
    display_chars(0, MONTH_SEGMENT, NULL, BLINK_ON);
}

static void edit_mo_dsel(void)
{
    display_chars(0, MONTH_SEGMENT, NULL, BLINK_OFF);
}

static void edit_mo_set(int8_t step)
{
    helpers_loop(&rtca_time.mon, 1, 12, step);

    auto_adjust_dd();
    rtca_update_dow();
    update_screen();
}

/* Day */
static void edit_dd_sel(void)
{
    lcd_screen_activate(0);
    display_chars(0, DAY_SEGMENT, NULL, BLINK_ON);
}

static void edit_dd_dsel(void)
{
    display_chars(0, DAY_SEGMENT, NULL, BLINK_OFF);
}

static void edit_dd_set(int8_t step)
{
    helpers_loop(&rtca_time.day, 1, rtca_get_max_days(rtca_time.mon,
                        rtca_time.year), step);
    rtca_update_dow();
    update_screen();
}

/* Hour */
static void edit_hh_sel(void)
{
    lcd_screen_activate(0);
    display_chars(0, LCD_SEG_L1_3_2, NULL, BLINK_ON);
}
static void edit_hh_dsel(void)
{
    display_chars(0, LCD_SEG_L1_3_2, NULL, BLINK_OFF);
}
static void edit_hh_set(int8_t step)
{
    helpers_loop(&rtca_time.hour, 0, 23, step);
    update_screen();
}


/* Minute */
static void edit_mm_sel(void)
{
    lcd_screen_activate(0);
    display_chars(0, LCD_SEG_L1_1_0, NULL, BLINK_ON);
}

static void edit_mm_dsel(void)
{
    display_chars(0, LCD_SEG_L1_1_0, NULL, BLINK_OFF);
}

static void edit_mm_set(int8_t step)
{
    helpers_loop(&rtca_time.min, 0, 59, step);
    update_screen();
}

/* 12h/24h */
static void edit_12_24_display(void)
{
    display_chars(2, LCD_SEG_L1_3_0 , display_am_pm ? " 12H" : " 24H", SEG_SET);
}

static void edit_12_24_sel(void)
{
    lcd_screen_activate(2);
    edit_12_24_display();
    display_chars(2, LCD_SEG_L1_3_0, NULL, BLINK_ON);
}

static void edit_12_24_dsel(void)
{
    display_chars(2, LCD_SEG_L1_3_0, NULL, BLINK_OFF);
}

static void edit_12_24_set(int8_t step)
{
    display_am_pm ^= 1;
    edit_12_24_display();
    update_screen();
}

/* Save YMDHMS */
static void edit_save()
{
    /* Here we return from the edit mode, fill in the new values! */
    rtca_time.sec = 0;
    rtca_set_time();
    rtca_set_date();

    /* turn off only SOME blinking segments */
    display_chars(0, LCD_SEG_L1_3_0, NULL, BLINK_OFF);
    display_chars(0, LCD_SEG_L2_4_0, NULL, BLINK_OFF);
    display_chars(1, LCD_SEG_L1_3_0, NULL, BLINK_OFF);
    display_chars(2, LCD_SEG_L1_3_0, NULL, BLINK_OFF);

    /* return to main screen */
    lcd_screen_activate(0);

    /* start the RTC */
    rtca_start();

    /* update screens with fake event */
    update_screen();
}

/* edit mode item table */
static struct menu_editmode_item edit_items[] = {
    {&edit_hh_sel, &edit_hh_dsel, &edit_hh_set},
    {&edit_mm_sel, &edit_mm_dsel, &edit_mm_set},
    {&edit_yy_sel, &edit_yy_dsel, &edit_yy_set},
    {&edit_mo_sel, &edit_mo_dsel, &edit_mo_set},
    {&edit_dd_sel, &edit_dd_dsel, &edit_dd_set},
    {&edit_12_24_sel, &edit_12_24_dsel, &edit_12_24_set},
    { NULL },
};

/************************ menu callbacks **********************************/
static void clock_activated()
{
    sys_messagebus_register(&clock_event,
                            SYS_MSG_RTC_YEAR | SYS_MSG_RTC_MONTH | SYS_MSG_RTC_DAY |
                            SYS_MSG_RTC_HOUR | SYS_MSG_RTC_MINUTE | SYS_MSG_RTC_SECOND
    );

    /* create three screens, the first is always the active one */
    lcd_screens_create(3); // 0:time + date, 1: year + day of week, 2:temp for settings (ex 12/24h setup)

    display_symbol(0, LCD_SEG_L1_COL, SEG_ON);

    /* update screens with fake event */
    update_screen();
}

static void clock_deactivated()
{
    sys_messagebus_unregister_all(&clock_event);

    /* destroy virtual screens */
    lcd_screens_destroy();

    /* clean up screen */
    display_symbol(0, LCD_SEG_L1_COL, SEG_OFF);
    display_symbol(0, LCD_SYMB_PM, SEG_OFF);

    display_clear(0, 1);
    display_clear(0, 2);
}

/* Num button press callback */
static void num_pressed()
{
    uint8_t index = get_active_lcd_screen_nr();
    if (++index >= 2) {
        index = 0; // Skip setup screen
    }

    lcd_screen_activate(index);
}

/* Star button long press callback. */
static void star_long_pressed()
{
    /* stop the hardware RTC */
    rtca_stop();

#ifdef CONFIG_MOD_CLOCK_BLINKCOL
    /* the blinking dots feature might hide the two dots, we display them here just in case */
    display_symbol(0, LCD_SEG_L1_COL, SEG_ON);
#endif
    display_seconds = 0;
    update_screen();
    menu_editmode_start(&edit_save, edit_items);
}

static void up_down_pressed()
{
    display_seconds ^= 1;
    display_clear(0, 2);

    update_screen();
}

void mod_clock_init()
{
    menu_add_entry("CLOCK",
                   &up_down_pressed,
                   &up_down_pressed,
                   &num_pressed,
                   &star_long_pressed,
                   NULL,
                   NULL,
                   &clock_activated,
                   &clock_deactivated);
}
