/**
 * @file evdev.h
 *
 */

#ifndef EVDEV_H
#define EVDEV_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#ifndef LV_DRV_NO_CONF
#ifdef LV_CONF_INCLUDE_SIMPLE
#include "lv_drv_conf.h"
#else
#include "../../lv_drv_conf.h"
#endif
#endif

#if USE_EVDEV || USE_BSD_EVDEV

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#if USE_BSD_EVDEV
#include <dev/evdev/input.h>
#else
#include <linux/input.h>
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct
{
    int fd;

    int x;
    int y;

    struct input_absinfo x_absinfo;
    int x_max;
    struct input_absinfo y_absinfo;
    int y_max;

    int key_val;
    int button;
    bool abs_mode;
    bool rel_mode;
} evdev_data_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Initialize and register the evdev driver
 * @param dev_name set the evdev device filename
 * @param type input device type
 * @param indev output value for lv_indev_t
 * @return true: the device file set complete
 *         false: the device file doesn't exist current system
 */
bool evdev_register(const char* dev_name, lv_indev_type_t type, lv_indev_t** indev_p);
/**
 * Get the current position and state of the evdev
 * @param data store the evdev data here
 * @return false: because the points are not buffered, so no more data to be read
 */
bool evdev_read(lv_indev_drv_t* drv, lv_indev_data_t* data);

/**********************
 *      MACROS
 **********************/

#endif /* USE_EVDEV */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EVDEV_H */
