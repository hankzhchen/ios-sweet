/*
 * Minimal text console on the ABL splash framebuffer. With the MMU off
 * every access is Device memory, so stay aligned (-mstrict-align).
 */
#include "console.h"
#include "font.h"

static int con_scale = 3;
#define CH_W		(8 * con_scale)
#define CH_H		(16 * con_scale)
#define MARGIN		24
#define LABEL_COLS	8

volatile uint32_t *con_fb = (volatile uint32_t *)FB_BASE;
static uint32_t con_bg;
static int row;

void con_clear(uint32_t bg)
{
	for (int y = 0; y < FB_H; y++) {
		volatile uint32_t *line = con_fb + y * (FB_STRIDE / 4);
		for (int x = 0; x < FB_W; x++)
			line[x] = bg;
	}
	con_bg = bg;
	row = 0;
}

static void put_char(int col, char ch, uint32_t fg)
{
	if (ch < 0x20 || ch > 0x7e)
		ch = '?';
	const unsigned char *g = font8x16[ch - 0x20];
	int x0 = MARGIN + col * CH_W, y0 = MARGIN + row * CH_H;
	if (x0 + CH_W > FB_W || y0 + CH_H > FB_H)
		return;
	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 8; gx++) {
			uint32_t c = (g[gy] & (0x80 >> gx)) ? fg : con_bg;
			for (int sy = 0; sy < con_scale; sy++) {
				volatile uint32_t *p = con_fb + (y0 + gy * con_scale + sy) * (FB_STRIDE / 4)
						       + x0 + gx * con_scale;
				for (int sx = 0; sx < con_scale; sx++)
					p[sx] = c;
			}
		}
}

static int put_str(int col, const char *s, uint32_t fg)
{
	while (*s)
		put_char(col++, *s++, fg);
	return col;
}

static void fmt_hex(char *buf, uint64_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < digits; i++)
		buf[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	buf[digits] = 0;
}

void con_set_scale(int scale)
{
	con_scale = scale;
}

/* Show the end of a text: as many trailing lines as fit, long lines wrapped. */
void con_tail(const char *t, unsigned len)
{
	int cols = (FB_W - 2 * MARGIN) / CH_W, rows = (FB_H - 2 * MARGIN) / CH_H - row;
	unsigned first = len, end = len;
	int used = 0;

	if (end && t[end - 1] == '\n')
		end--;
	len = end;
	while (end > 0) {
		unsigned ls = end;
		while (ls > 0 && t[ls - 1] != '\n')
			ls--;
		int n = (int)(end - ls), r = n ? (n + cols - 1) / cols : 1;
		if (used + r > rows)
			break;
		used += r;
		first = ls;
		if (!ls)
			break;
		end = ls - 1;	/* skip the newline */
	}
	int col = 0;
	for (unsigned i = first; i < len; i++) {
		char ch = t[i];
		if (ch == '\r')
			continue;
		if (ch == '\n') {
			row++;
			col = 0;
			continue;
		}
		if (col == cols) {
			row++;
			col = 0;
		}
		put_char(col++, ch, WHITE);
	}
	row++;
}

void con_skip(void)
{
	row++;
}

void con_str(const char *label, const char *s)
{
	put_str(0, label, YELLOW);
	put_str(LABEL_COLS, s, WHITE);
	row++;
}

void con_hex(const char *label, uint64_t v)
{
	char buf[17];
	fmt_hex(buf, v, 16);
	con_str(label, buf);
}

void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb)
{
	char buf[9];
	int col = put_str(0, label, YELLOW);
	col = col < LABEL_COLS ? LABEL_COLS : col + 1;
	col = put_str(col, a, YELLOW);
	fmt_hex(buf, va, 8);
	col = put_str(col, buf, WHITE);
	if (b) {
		col = put_str(col + 1, b, YELLOW);
		fmt_hex(buf, vb, 8);
		put_str(col, buf, WHITE);
	}
	row++;
}
