/* tux-splash.c — draw one Tux penguin per online CPU at the top of /dev/fb0.
 *
 * Reads the framebuffer geometry, counts online CPUs, and blits that many
 * 80x80 Tux logos (centered at the top). Pure black (0,0,0) is treated as
 * transparent. Handles 32bpp XRGB8888 and 16bpp RGB565.
 *
 * Compiled statically for arm64 and shipped inside the initramfs; run by
 * initramfs/init once /dev/fb0 exists.
 */
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "tux.h"

int main(void)
{
	int fd = open("/dev/fb0", O_RDWR);
	if (fd < 0) {
		perror("open /dev/fb0");
		return 1;
	}

	struct fb_var_screeninfo v;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &v)) {
		perror("FBIOGET_VSCREENINFO");
		close(fd);
		return 1;
	}
	struct fb_fix_screeninfo f;
	if (ioctl(fd, FBIOGET_FSCREENINFO, &f)) {
		perror("FBIOGET_FSCREENINFO");
		close(fd);
		return 1;
	}

	if (v.bits_per_pixel != 16 && v.bits_per_pixel != 32) {
		fprintf(stderr, "unsupported bpp %u\n", v.bits_per_pixel);
		close(fd);
		return 1;
	}

	unsigned char *fb = mmap(NULL, f.smem_len, PROT_READ | PROT_WRITE,
				 MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return 1;
	}

	long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpu < 1)
		ncpu = 1;
	if (ncpu > 8)
		ncpu = 8;

	int lw = f.line_length;
	int bpp = v.bits_per_pixel / 8;
	int total_w = TUX_W * ncpu;
	int x0 = (v.xres > (unsigned)total_w) ? (v.xres - total_w) / 2 : 0;
	int y0 = 8; /* a little below the very top edge */

	for (int c = 0; c < ncpu; c++) {
		for (int y = 0; y < TUX_H; y++) {
			for (int x = 0; x < TUX_W; x++) {
				int r = tux[y][x][0], g = tux[y][x][1],
				    b = tux[y][x][2];
				if (r == 0 && g == 0 && b == 0)
					continue; /* transparent */

				int px = x0 + c * TUX_W + x;
				int py = y0 + y;
				if (px < 0 || px >= (int)v.xres || py < 0 ||
				    py >= (int)v.yres)
					continue;

				size_t off = (size_t)py * lw + (size_t)px * bpp;
				if (bpp == 4) {
					unsigned int val =
						((unsigned)r << v.red.offset) |
						((unsigned)g << v.green.offset) |
						((unsigned)b << v.blue.offset);
					*(unsigned int *)(fb + off) = val;
				} else {
					unsigned short val =
						((r >> 3) << v.red.offset) |
						((g >> 2) << v.green.offset) |
						((b >> 3) << v.blue.offset);
					*(unsigned short *)(fb + off) = val;
				}
			}
		}
	}

	munmap(fb, f.smem_len);
	close(fd);
	return 0;
}
