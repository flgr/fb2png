//-------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Copyright (c) 2013 Andrew Duncan
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
// CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------

#include <errno.h>
#include <fcntl.h>
#include <png.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

int main(int argc, char *argv[])
{
    char *program = argv[0];
    char *fbdevice = "/dev/fb0";
    char *pngname = "fb.png";

    int opt = 0;
    int opt_compression_level = -1;
    int opt_skip_png_creation = 0;
    int opt_width = -1;
    int opt_height = -1;
    int opt_xoff = 0;
    int opt_yoff = 0;
    int opt_xadv = 1;
    int opt_yadv = 1;

    //--------------------------------------------------------------------

    while ((opt = getopt(argc, argv, "d:p:b:x:s:t:y:w:h:z:")) != -1)
    {
        switch (opt)
        {
        case 'd':
            fbdevice = optarg;
            break;

        case 'p':
            pngname = optarg;
            opt_skip_png_creation = strcmp("skip", pngname) == 0;
            break;

        case 'z':
            opt_compression_level = atoi(optarg);
            break;

        case 's':
            opt_yadv = 1+atoi(optarg);
            break;

        case 't':
            opt_xadv = 1+atoi(optarg);
            break;

        case 'x':
            opt_xoff = atoi(optarg);
            break;

        case 'y':
            opt_yoff = atoi(optarg);
            break;

        case 'w':
            opt_width = atoi(optarg);
            break;

        case 'h':
            opt_height = atoi(optarg);
            break;

        default:
            fprintf(stderr,
                    "Usage: %s [-d device] [-p pngname]\n",
                    program);
            exit(EXIT_FAILURE);
            break;
        }
    }

    //--------------------------------------------------------------------

    int fbfd = open(fbdevice, O_RDWR);

    if (fbfd == -1)
    {
        fprintf(stderr,
                "%s: cannot open framebuffer - %s",
                program,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct fb_fix_screeninfo finfo;

    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1)
    {
        fprintf(stderr,
                "%s: reading framebuffer fixed information - %s",
                program,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1)
    {
        fprintf(stderr,
                "%s: reading framebuffer variable information - %s",
                program,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((vinfo.bits_per_pixel != 16) &&
        (vinfo.bits_per_pixel != 24) &&
        (vinfo.bits_per_pixel != 32))
    {
        fprintf(stderr, "%s: only 16, 24 and 32 ", program);
        fprintf(stderr, "bits per pixels supported\n");
        exit(EXIT_FAILURE);
    }

    void *memp = mmap(0,
                      finfo.smem_len,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      fbfd,
                      0);

    close(fbfd);
    fbfd = -1;

    if (memp == MAP_FAILED)
    {
        fprintf(stderr,
                "%s: failed to map framebuffer device to memory - %s",
                program,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (opt_skip_png_creation) {
        return 0;
    }

    uint8_t *fbp = memp;
    memp = NULL;

    //--------------------------------------------------------------------

    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                                  NULL,
                                                  NULL,
                                                  NULL);

    if (png_ptr == NULL)
    {
        fprintf(stderr,
                "%s: could not allocate PNG write struct\n",
                program);
        exit(EXIT_FAILURE);
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);

    if (info_ptr == NULL)
    {
        fprintf(stderr,
                "%s: could not allocate PNG info struct\n",
                program);
        exit(EXIT_FAILURE);
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);

        fprintf(stderr, "%s: error creating PNG\n", program);
        exit(EXIT_FAILURE);
    }

    FILE *pngfp = fopen(pngname, "wb");

    if (pngfp == NULL)
    {
        fprintf(stderr, "%s: Unable to create %s\n", program, pngname);
        exit(EXIT_FAILURE);
    }

    png_init_io(png_ptr, pngfp);
    int width = opt_width == -1 ? vinfo.xres : opt_width;
    int rawwidth = width;
    int height = opt_height == -1 ? vinfo.yres : opt_height;
    int rawheight = height;

    width /= opt_xadv;
    height /= opt_yadv;

    png_set_IHDR(
        png_ptr,
        info_ptr,
        width,
        height,
        8,
        PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE,
        PNG_FILTER_TYPE_BASE);

    if (opt_compression_level != -1) {
        png_set_compression_level(png_ptr, opt_compression_level);
    }

    png_write_info(png_ptr, info_ptr);

    png_bytep png_buffer = malloc(width * 3 * sizeof(png_byte));

    if (png_buffer == NULL)
    {
        fprintf(stderr, "%s: Unable to allocate buffer\n", program);
        exit(EXIT_FAILURE);
    }

    //--------------------------------------------------------------------

    int r_mask = (1 << vinfo.red.length) - 1;
    int g_mask = (1 << vinfo.green.length) - 1;
    int b_mask = (1 << vinfo.blue.length) - 1;

    int bytes_per_pixel = vinfo.bits_per_pixel / 8;

    int y = 0;

    for (y = opt_yoff; y < opt_yoff + rawheight; y += opt_yadv)
    {
        int x;
        int pb_offset = 0;

        for (x = opt_xoff; x < opt_xoff + rawwidth; x += opt_xadv)
        {
            size_t fb_offset = (vinfo.xoffset + x) * (bytes_per_pixel)
                             + (vinfo.yoffset + y) * finfo.line_length;

            uint32_t pixel = 0;

            switch (vinfo.bits_per_pixel)
            {
            case 16:

                pixel = *((uint16_t *)(fbp + fb_offset));
                break;

            case 24:

                pixel += *(fbp + fb_offset);
                pixel += *(fbp + fb_offset + 1) << 8;
                pixel += *(fbp + fb_offset + 2) << 16;
                break;

            case 32:

                pixel = *((uint32_t *)(fbp + fb_offset));
                break;

            default:

                // nothing to do
                break;
            }

            png_byte r = (pixel >> vinfo.red.offset) & r_mask;
            png_byte g = (pixel >> vinfo.green.offset) & g_mask;
            png_byte b = (pixel >> vinfo.blue.offset) & b_mask;

            png_buffer[pb_offset] = (r * 0xFF) / r_mask;
            png_buffer[pb_offset + 1] = (g * 0xFF)  / g_mask;
            png_buffer[pb_offset + 2] = (b * 0xFF)  / b_mask;

            pb_offset += 3;
        }

        png_write_row(png_ptr, png_buffer);
    }

    //--------------------------------------------------------------------

    free(png_buffer);
    png_buffer = NULL;
    png_write_end(png_ptr, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(pngfp);

    //--------------------------------------------------------------------

    munmap(fbp, finfo.smem_len);

    //--------------------------------------------------------------------

    return 0;
}
