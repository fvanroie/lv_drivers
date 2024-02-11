/**
 * @file evdev.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "evdev.h"
#if USE_EVDEV != 0 || USE_BSD_EVDEV

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
int map(int x, int in_min, int in_max, int out_min, int out_max);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Initialize the evdev interface
 */
/**
 * Initialize and register the evdev driver
 * @param dev_name set the evdev device filename
 * @param type input device type
 * @param indev output value for lv_indev_t
 * @return true: the device file set complete
 *         false: the device file doesn't exist current system
 */
bool evdev_register(const char* dev_name, lv_indev_type_t type, lv_indev_t** indev_p)
{
#if USE_BSD_EVDEV
    int evdev_fd = open(dev_name, O_RDWR | O_NOCTTY);
#else
    int evdev_fd = open(dev_name, O_RDWR | O_NOCTTY | O_NDELAY);
#endif

    if(evdev_fd == -1) {
        perror("evdev_register(): open failed");
        return false;
    }

#if USE_BSD_EVDEV
    fcntl(evdev_fd, F_SETFL, O_NONBLOCK);
#else
    fcntl(evdev_fd, F_SETFL, O_ASYNC | O_NONBLOCK);
#endif

    evdev_data_t* user_data = malloc(sizeof(evdev_data_t));
    if(user_data == NULL) {
        return false;
    }
    memset(user_data, 0x00, sizeof(evdev_data_t));
    user_data->fd = evdev_fd;

    // find ABS_X/Y min/max values, ignore errors
    if(ioctl(evdev_fd, EVIOCGABS(ABS_X), &user_data->x_absinfo) < 0)
        ioctl(evdev_fd, EVIOCGABS(ABS_MT_POSITION_X), &user_data->x_absinfo);
    if(ioctl(evdev_fd, EVIOCGABS(ABS_Y), &user_data->y_absinfo) < 0)
        ioctl(evdev_fd, EVIOCGABS(ABS_MT_POSITION_Y), &user_data->y_absinfo);
    // find display size
    user_data->x_max = lv_disp_get_hor_res(NULL);
    user_data->y_max = lv_disp_get_ver_res(NULL);

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.user_data = user_data;
    indev_drv.type      = type;
    indev_drv.read_cb   = evdev_read;
    lv_indev_t* indev   = lv_indev_drv_register(&indev_drv);

    if(indev_p != NULL) {
        *indev_p = indev;
    }

    return indev != NULL;
}
/**
 * Get the current position and state of the evdev
 * @param data store the evdev data here
 * @return false: because the points are not buffered, so no more data to be read
 */
bool evdev_read(lv_indev_drv_t* drv, lv_indev_data_t* data)
{
    struct input_event in;

    evdev_data_t* user_data = (evdev_data_t*)drv->user_data;

    while(read(user_data->fd, &in, sizeof(struct input_event)) > 0) {
        if(in.type == EV_REL) {
            user_data->abs_mode = false;
            user_data->rel_mode = true;
            if(in.code == REL_X)
                user_data->x += in.value;
            else if(in.code == REL_Y)
                user_data->y += in.value;
        } else if(in.type == EV_ABS) {
            user_data->abs_mode = true;
            user_data->rel_mode = false;
            if(in.code == ABS_X)
                user_data->x = in.value;
            else if(in.code == ABS_Y)
                user_data->y = in.value;
            else if(in.code == ABS_MT_SLOT)
                user_data->mt_ignore = in.value != 1;
            if(!user_data->mt_ignore) {
                if(in.code == ABS_MT_POSITION_X)
                    user_data->x = in.value;
                else if(in.code == ABS_MT_POSITION_Y)
                    user_data->y = in.value;
                else if(in.code == ABS_MT_TRACKING_ID) {
                    if(in.value > 0)
                        user_data->button = LV_INDEV_STATE_PR;
                    else
                        user_data->button = LV_INDEV_STATE_REL;
                }
            }
        } else if(in.type == EV_KEY) {
            if(in.code == BTN_MOUSE || in.code == BTN_TOUCH) {
                if(in.value == 0)
                    user_data->button = LV_INDEV_STATE_REL;
                else if(in.value == 1)
                    user_data->button = LV_INDEV_STATE_PR;
            } else if(drv->type == LV_INDEV_TYPE_KEYPAD) {
                data->state = (in.value) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
                switch(in.code) {
                    case KEY_BACKSPACE:
                        data->key = LV_KEY_BACKSPACE;
                        break;
                    case KEY_ENTER:
                        data->key = LV_KEY_ENTER;
                        break;
                    case KEY_UP:
                        data->key = LV_KEY_UP;
                        break;
                    case KEY_LEFT:
                        data->key = LV_KEY_PREV;
                        break;
                    case KEY_RIGHT:
                        data->key = LV_KEY_NEXT;
                        break;
                    case KEY_DOWN:
                        data->key = LV_KEY_DOWN;
                        break;
                    default:
                        data->key = 0;
                        break;
                }
                user_data->key_val = data->key;
                user_data->button  = data->state;
                return false;
            }
        }
    }

    if(drv->type == LV_INDEV_TYPE_KEYPAD) {
        /* No data retrieved */
        data->key   = user_data->key_val;
        data->state = user_data->button;
        return false;
    }
    if(drv->type != LV_INDEV_TYPE_POINTER) return false;
    /*Store the collected data*/

    if(user_data->rel_mode) {
        // relative mode has no calibration/scaling - make sure it's within bounds at all times
        if(user_data->x < 0) user_data->x = 0;
        if(user_data->y < 0) user_data->y = 0;
        if(user_data->x >= user_data->x_max) user_data->x = user_data->x_max - 1;
        if(user_data->y >= user_data->y_max) user_data->y = user_data->y_max - 1;
    }

    int x = user_data->x;
    int y = user_data->y;
    if(user_data->abs_mode) {
        // absolute mode can be calibrated or scaled automatically
#if EVDEV_CALIBRATE
        x = map(x, EVDEV_HOR_MIN, EVDEV_HOR_MAX, 0, user_data->x_max);
        y = map(y, EVDEV_VER_MIN, EVDEV_VER_MAX, 0, user_data->y_max);
#else
        if(user_data->x_absinfo.minimum || user_data->x_absinfo.maximum) {
            x = map(x, user_data->x_absinfo.minimum, user_data->x_absinfo.maximum, 0, user_data->x_max);
        }
        if(user_data->y_absinfo.minimum || user_data->y_absinfo.maximum) {
            y = map(y, user_data->y_absinfo.minimum, user_data->y_absinfo.maximum, 0, user_data->y_max);
        }
#endif
    }

#if !EVDEV_SWAP_AXES
    data->point.x = x;
    data->point.y = y;
#else
    data->point.x = y;
    data->point.y = x;
#endif

    data->state = user_data->button;

    return false;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
int map(int x, int in_min, int in_max, int out_min, int out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#endif
