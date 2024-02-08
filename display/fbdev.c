/**
 * @file fbdev.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "fbdev.h"
#if USE_FBDEV || USE_BSD_FBDEV

#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#if USE_BSD_FBDEV
#include <sys/fcntl.h>
#include <sys/time.h>
#include <sys/consio.h>
#include <sys/fbio.h>
#else /* USE_BSD_FBDEV */
#include <linux/fb.h>
#endif /* USE_BSD_FBDEV */

/*********************
 *      DEFINES
 *********************/
#ifndef FBDEV_PATH
#define FBDEV_PATH "/dev/fb0"
#endif

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *      STRUCTURES
 **********************/

struct bsd_fb_var_info
{
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t xres;
    uint32_t yres;
    int bits_per_pixel;
};

struct bsd_fb_fix_info
{
    long int line_length;
    long int smem_len;
};

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
#if USE_BSD_FBDEV
static struct bsd_fb_var_info vinfo;
static struct bsd_fb_fix_info finfo;
#else
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
#endif /* USE_BSD_FBDEV */
static char* fbp           = 0;
static long int screensize = 0;
static int fbfd            = 0;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void fbdev_init(void)
{
    // Open the file for reading and writing
    fbfd = open(FBDEV_PATH, O_RDWR);
    if(fbfd == -1) {
        perror("Error: cannot open framebuffer device");
        return;
    }

#if USE_BSD_FBDEV
    struct fbtype fb;
    unsigned line_length;

    // Get fb type
    if(ioctl(fbfd, FBIOGTYPE, &fb) != 0) {
        perror("ioctl(FBIOGTYPE)");
        return;
    }

    // Get screen width
    if(ioctl(fbfd, FBIO_GETLINEWIDTH, &line_length) != 0) {
        perror("ioctl(FBIO_GETLINEWIDTH)");
        return;
    }

    vinfo.xres           = (unsigned)fb.fb_width;
    vinfo.yres           = (unsigned)fb.fb_height;
    vinfo.bits_per_pixel = fb.fb_depth;
    vinfo.xoffset        = 0;
    vinfo.yoffset        = 0;
    finfo.line_length    = line_length;
    finfo.smem_len       = finfo.line_length * vinfo.yres;
#else  /* USE_BSD_FBDEV */

    // Get fixed screen information
    if(ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        perror("Error reading fixed information");
        return;
    }

    // Get variable screen information
    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        perror("Error reading variable information");
        return;
    }
#endif /* USE_BSD_FBDEV */

    printf("%dx%d, %dbpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

    // Figure out the size of the screen in bytes
    screensize = finfo.smem_len; // finfo.line_length * vinfo.yres;

    // Map the device to memory
    fbp = (char*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if((intptr_t)fbp == -1) {
        perror("Error: failed to map framebuffer device to memory");
        return;
    }
    memset(fbp, 0, screensize);

    static lv_disp_buf_t disp_buf;
    lv_disp_buf_init(&disp_buf, (lv_color_t*)malloc(vinfo.xres * vinfo.yres * sizeof(lv_color_t)), NULL,
                     vinfo.xres * vinfo.yres);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = vinfo.xres;
    disp_drv.ver_res  = vinfo.yres;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.buffer   = &disp_buf;
    lv_disp_drv_register(&disp_drv);
}

void fbdev_exit(void)
{
    close(fbfd);
}

/**
 * Flush a buffer to the marked area
 * @param drv pointer to driver where this function belongs
 * @param area an area where to copy `color_p`
 * @param color_p an array of pixel to copy to the `area` part of the screen
 */
void fbdev_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p)
{
    if(fbp == NULL || area->x2 < 0 || area->y2 < 0 || area->x1 > (int32_t)vinfo.xres - 1 ||
       area->y1 > (int32_t)vinfo.yres - 1) {
        lv_disp_flush_ready(drv);
        return;
    }

    /*Truncate the area to the screen*/
    int32_t act_x1 = area->x1 < 0 ? 0 : area->x1;
    int32_t act_y1 = area->y1 < 0 ? 0 : area->y1;
    int32_t act_x2 = area->x2 > (int32_t)vinfo.xres - 1 ? (int32_t)vinfo.xres - 1 : area->x2;
    int32_t act_y2 = area->y2 > (int32_t)vinfo.yres - 1 ? (int32_t)vinfo.yres - 1 : area->y2;

    lv_coord_t w               = (act_x2 - act_x1 + 1);
    long int location          = 0;
    long int byte_location     = 0;
    unsigned char bit_location = 0;

    uint32_t* fbp32 = (uint32_t*)fbp;
    uint16_t* fbp16 = (uint16_t*)fbp;
    uint8_t* fbp8   = (uint8_t*)fbp;
    int32_t x, y;

    if(LV_COLOR_DEPTH == vinfo.bits_per_pixel) {
        /*32 or 24 bit per pixel*/
        if(vinfo.bits_per_pixel == 32 || vinfo.bits_per_pixel == 24) {
            for(y = act_y1; y <= act_y2; y++) {
                location = (act_x1 + vinfo.xoffset) + (y + vinfo.yoffset) * finfo.line_length / 4;
                memcpy(&fbp32[location], (uint32_t*)color_p, (act_x2 - act_x1 + 1) * 4);
                color_p += w;
            }
        }
        /*16 bit per pixel*/
        else if(vinfo.bits_per_pixel == 16) {
            for(y = act_y1; y <= act_y2; y++) {
                location = (act_x1 + vinfo.xoffset) + (y + vinfo.yoffset) * finfo.line_length / 2;
                memcpy(&fbp16[location], (uint32_t*)color_p, (act_x2 - act_x1 + 1) * 2);
                color_p += w;
            }
        }
        /*8 bit per pixel*/
        else if(vinfo.bits_per_pixel == 8) {
            for(y = act_y1; y <= act_y2; y++) {
                location = (act_x1 + vinfo.xoffset) + (y + vinfo.yoffset) * finfo.line_length;
                memcpy(&fbp8[location], (uint32_t*)color_p, (act_x2 - act_x1 + 1));
                color_p += w;
            }
        }
        /*1 bit per pixel*/
        else if(vinfo.bits_per_pixel == 1) {
            for(y = act_y1; y <= act_y2; y++) {
                for(x = act_x1; x <= act_x2; x++) {
                    location      = (x + vinfo.xoffset) + (y + vinfo.yoffset) * vinfo.xres;
                    byte_location = location / 8; /* find the byte we need to change */
                    bit_location  = location % 8; /* inside the byte found, find the bit we need to change */
                    fbp8[byte_location] &= ~(((uint8_t)(1)) << bit_location);
                    fbp8[byte_location] |= ((uint8_t)(color_p->full)) << bit_location;
                    color_p++;
                }

                color_p += area->x2 - act_x2;
            }
        } else {
            /*Not supported bit per pixel*/
        }
    } else { // LV_COLOR_DEPTH != vinfo.bits_per_pixel
        /*32 bit per pixel*/
        if(vinfo.bits_per_pixel == 32) {
            for(y = area->y1; y <= area->y2 && y < vinfo.yres; y++) {
                for(x = area->x1; x <= area->x2; x++) {
                    // UNTESTED, MIGHT NEED A BYTE SWAP
                    fbp32[(y * vinfo.xres + x)] = lv_color_to32(*color_p);
                    color_p++;
                }
            }
        }
        /*24 bit per pixel*/
        else if(vinfo.bits_per_pixel == 24) {
            for(y = area->y1; y <= area->y2 && y < vinfo.yres; y++) {
                int32_t y_offset = (y * vinfo.xres + area->x1) * 3;
                for(x = area->x1; x <= area->x2; x++) {
                    uint32_t color = lv_color_to32(*color_p);
                    color_p++;
                    fbp8[y_offset++] = (color >> 0) & 0xFF;  // B
                    fbp8[y_offset++] = (color >> 8) & 0xFF;  // G
                    fbp8[y_offset++] = (color >> 16) & 0xFF; // R
                }
            }
        }
        /*16 bit per pixel*/
        else if(vinfo.bits_per_pixel == 16) {
            for(y = area->y1; y <= area->y2 && y < vinfo.yres; y++) {
                for(x = area->x1; x <= area->x2; x++) {
                    // UNTESTED, MIGHT NEED A BYTE SWAP
                    fbp16[(y * vinfo.xres + x)] = lv_color_to16(*color_p);
                    color_p++;
                }
            }
        }
        /*8 bit per pixel*/
        else if(vinfo.bits_per_pixel == 8) {
            for(y = area->y1; y <= area->y2 && y < vinfo.yres; y++) {
                int32_t y_offset = (y * vinfo.xres + x) * 3;
                for(x = area->x1; x <= area->x2; x++) {
                    // UNTESTED
                    fbp8[(y * vinfo.xres + x)] = lv_color_to8(*color_p);
                    color_p++;
                }
            }
        } else {
            /*Not supported bit per pixel*/
        }
    }

    // May be some direct update command is required
    // ret = ioctl(state->fd, FBIO_UPDATE, (unsigned long)((uintptr_t)rect));

    lv_disp_flush_ready(drv);
}

static inline void fbdev_put_color(int pos, uint32_t color)
{
    uint32_t* fbp32 = (uint32_t*)fbp;
    uint16_t* fbp16 = (uint16_t*)fbp;
    uint8_t* fbp8   = (uint8_t*)fbp;
    switch(vinfo.bits_per_pixel) {
        case 32:
            fbp32[pos] = color;
            break;
        case 24:
            fbp8[pos * 3 + 0] = (color >> 0) & 0xFF;  // B
            fbp8[pos * 3 + 1] = (color >> 8) & 0xFF;  // G
            fbp8[pos * 3 + 2] = (color >> 16) & 0xFF; // R
            break;
        case 16:
            fbp16[pos] = color;
            break;
        case 8:
            fbp8[pos] = color;
            break;
    }
}

void fbdev_splashscreen(const uint8_t* logoImage, size_t logoWidth, size_t logoHeight, lv_color_t fgColor,
                        lv_color_t bgColor)
{

    int x = (vinfo.xres - logoWidth) / 2;
    int y = (vinfo.yres - logoHeight) / 2;
    int32_t i, j, byteWidth = (logoWidth + 7) / 8;

    uint32_t bgColorNum, fgColorNum;
    switch(vinfo.bits_per_pixel) {
        case 32:
        case 24:
            fgColorNum = lv_color_to32(fgColor);
            bgColorNum = lv_color_to32(bgColor);
            break;
        case 16:
            fgColorNum = lv_color_to16(fgColor);
            bgColorNum = lv_color_to16(bgColor);
            break;
        case 8:
            fgColorNum = lv_color_to8(fgColor);
            bgColorNum = lv_color_to8(bgColor);
            break;
    }

    for(size_t y = 0; y < vinfo.xres * vinfo.yres; y++) {
        fbdev_put_color(y, bgColorNum);
    }

    for(j = 0; j < logoHeight; j++) {
        for(i = 0; i < logoWidth; i++) {
            if(logoImage[j * byteWidth + i / 8] & (1 << (i & 7))) {
                fbdev_put_color((y + j) * vinfo.xres + x + i, fgColorNum);
            }
        }
    }
}

void fbdev_get_sizes(uint32_t* width, uint32_t* height)
{
    if(width) *width = vinfo.xres;

    if(height) *height = vinfo.yres;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

#endif
