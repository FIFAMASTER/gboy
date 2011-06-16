// gboy - a portable gameboy emulator
// Copyright (C) 2011  Garrett Smith.
// 
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version.
// 
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.
// 
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include <assert.h>
#include "gbx.h"
#include "memory.h"
#include "ports.h"

// Gameboy Speed:   4.194304 MHz
// Horizontal Sync: 9198 Hz (108.719287 us)
// Vertical Sync:   59.73 Hz (16.7420057 ms)

// Refresh Cycles:  70224 (17556)
// VBLANK Cycles:   4560 (1140)

// Total Lines:     154 (LY =   0..153)
// Visible Lines:   144 (LY =   0..143)
// VBLANK Lines:    10  (LY = 144..153)

// Cycles Per Line: 4560 / 10 = 456 (114)
// Visible Pixels:  160 (40)
// Mode 0 Cycles:   204 (51)    reported 201-207
// Mode 2 Cycles:   80  (20)    reported 77-83
// Mode 3 Cycles:   172 (43)    reported 169-175

// -----------------------------------------------------------------------------
INLINE void sprite_normal(gbx_context_t *ctx, uint32_t *palette,
        uint8_t *tile, int xpos, int ypos, int w0, int h0, int w1, int h1)
{
    int x, y, w, h, val, a, b;
    for (h = h0; h <= h1; ++h) {
        a = *tile++;
        b = *tile++;
        for (w = w0; w <= w1; ++w) {
            x = xpos + w;
            y = ypos + h;
            val = ((a >> (7 - w)) & 1) | (((b >> (7 - w)) << 1) & 2);
            if (val) ctx->fb[y * GBX_LCD_XRES + x] = palette[val];
        }
    }
}

// -----------------------------------------------------------------------------
INLINE void sprite_xflip(gbx_context_t *ctx, uint32_t *palette,
        uint8_t *tile, int xpos, int ypos, int w0, int h0, int w1, int h1)
{
    int x, y, w, h, val, a, b;
    for (h = h0; h <= h1; ++h) {
        a = *tile++;
        b = *tile++;
        for (w = w0; w <= w1; ++w) {
            x = xpos + w;
            y = ypos + h;
            val = ((a >> w) & 1) | (((b >> w) << 1) & 2);
            if (val) ctx->fb[y * GBX_LCD_XRES + x] = palette[val];
        }
    }
}

// -----------------------------------------------------------------------------
INLINE void sprite_yflip(gbx_context_t *ctx, uint32_t *palette,
        uint8_t *tile, int xpos, int ypos, int w0, int h0, int w1, int h1)
{
    int x, y, w, h, val, a, b;
    for (h = h0; h <= h1; ++h) {
        b = *tile--;
        a = *tile--;
        for (w = w0; w <= w1; ++w) {
            x = xpos + w;
            y = ypos + h;
            val = ((a >> (7 - w)) & 1) | (((b >> (7 - w)) << 1) & 2);
            if (val) ctx->fb[y * GBX_LCD_XRES + x] = palette[val];
        }
    }
}

// -----------------------------------------------------------------------------
INLINE void sprite_xyflip(gbx_context_t *ctx, uint32_t *palette,
        uint8_t *tile, int xpos, int ypos, int w0, int h0, int w1, int h1)
{
    int x, y, w, h, val, a, b;
    for (h = h0; h <= h1; ++h) {
        b = *tile--;
        a = *tile--;
        for (w = w0; w <= w1; ++w) {
            x = xpos + w;
            y = ypos + h;
            val = ((a >> w) & 1) | (((b >> w) << 1) & 2);
            if (val) ctx->fb[y * GBX_LCD_XRES + x] = palette[val];
        }
    }
}

// -----------------------------------------------------------------------------
void render_sprites(gbx_context_t *ctx)
{
    int i, xpos, ypos, w0, h0, w1, h1, type;
    uint8_t pnum, attr;
    uint16_t oam_addr = 0xFE00;
    uint32_t *palette;
    uint8_t *tile;

    // check for 8x8 or 8x16 sprite, for 8x16 pattern LSB is ignored
    int SH = (ctx->video.lcdc & LCDC_OBJ_SIZE) ? 16 : 8;
    uint8_t pnum_mask = (ctx->video.lcdc & LCDC_OBJ_SIZE) ? 0xFE : 0xFF;

    for (i = 0; i < 40; ++i) {
        ypos = gbx_read_byte(ctx, oam_addr++);
        xpos = gbx_read_byte(ctx, oam_addr++);
        pnum = gbx_read_byte(ctx, oam_addr++) & pnum_mask;
        attr = gbx_read_byte(ctx, oam_addr++);
        if (!xpos && !ypos) continue;
        //if (!ypos || (ypos >= GBX_LCD_YRES)) continue;
        //if (!xpos || (xpos >= GBX_LCD_XRES)) continue; //XXX: alter sorting?

        // determine the orientation (normal, xflip, yflip, xflip & yflip)
        type = ((attr & OAM_ATTR_XFLIP) >> 5) | ((attr & OAM_ATTR_YFLIP) >> 5);

        // determine which color palette and VRAM bank we're indexing into
        if (ctx->cgb_enabled) {
            // select the palette base address (OBP0-7)
            palette = &ctx->video.ocpd_rgb[(attr & OAM_ATTR_CPAL) << 2];

            // select the tile data from either VRAM bank 0 or bank 1
            if (attr & OAM_ATTR_BANK)
                tile = &ctx->mem.vram[VRAM_BANK_SIZE + (pnum << 4)];
            else
                tile = &ctx->mem.vram[pnum << 4];
        }
        else {
            // monochrome mode: select from either OBP0 or OBP1
            if (attr & OAM_ATTR_PAL)
                palette = ctx->video.obp1_rgb;
            else
                palette = ctx->video.obp0_rgb;

            // tile data located in the first (and only) VRAM bank
            tile = &ctx->mem.vram[pnum << 4];
        }

        xpos -= 8;
        ypos -= 16;

        w0 = (xpos >= 0) ? 0 : -xpos;
        h0 = (ypos >= 0) ? 0 : -ypos;

        w1 = (xpos <= (GBX_LCD_XRES - 8)) ? 7 : (GBX_LCD_XRES - xpos - 1);
        h1 = (ypos <= (GBX_LCD_YRES - SH)) ? SH-1 : (GBX_LCD_YRES - ypos - 1);

        switch (type) {
        case 0:
            tile += (h0 << 1);
            sprite_normal(ctx, palette, tile, xpos, ypos, w0, h0, w1, h1);
            break;
        case 1:
            tile += (h0 << 1);
            sprite_xflip(ctx, palette, tile, xpos, ypos, w0, h0, w1, h1);
            break;
        case 2:
            tile += ((SH - 1 - h0) << 1) + 1;
            sprite_yflip(ctx, palette, tile, xpos, ypos, w0, h0, w1, h1);
            break;
        case 3:
            tile += ((SH - 1 - h0) << 1) + 1;
            sprite_xyflip(ctx, palette, tile, xpos, ypos, w0, h0, w1, h1);
            break;
        }
    }
}

// -----------------------------------------------------------------------------
void render_bg_pixel(gbx_context_t *ctx, int x, int y)
{
    int base_x, base_y, off_x, off_y, bit, mappos, cnum, color;
    uint8_t tile, c1, c2, attr;
    uint16_t addr;

    if ((ctx->video.lcdc & LCDC_WND_EN) &&
        (x >= (ctx->video.wx - 7)) && (y >= ctx->video.wy)) {
        base_x = x - (ctx->video.wx - 7);
        base_y = y - ctx->video.wy;
        mappos = ((base_y & 0xF8) << 2) | (base_x >> 3);

        off_x = base_x & 7;
        off_y = base_y & 7;
        bit = 7 - off_x;

        if (ctx->video.lcdc & LCDC_WND_CODE)
            tile = ctx->mem.vram[0x1C00 + mappos];
        else
            tile = ctx->mem.vram[0x1800 + mappos];

        if (ctx->video.lcdc & LCDC_BG_CHAR)
            addr = 0x8000 + (tile << 4) + (off_y << 1);
        else
            addr = 0x9000 + ((int8_t)tile << 4) + (off_y << 1);
    }
    else {
        base_x = (x + ctx->video.scx) & 0xFF;
        base_y = (y + ctx->video.scy) & 0xFF;
        mappos = ((base_y & 0xF8) << 2) | (base_x >> 3);

        off_x = base_x & 7;
        off_y = base_y & 7;
        bit = 7 - off_x;

        if (ctx->video.lcdc & LCDC_BG_CODE)
            tile = gbx_read_byte(ctx, 0x9C00 + mappos);
        else
            tile = gbx_read_byte(ctx, 0x9800 + mappos);

        if (ctx->video.lcdc & LCDC_BG_CHAR)
            addr = 0x8000 + (tile << 4) + (off_y << 1);
        else
            addr = 0x9000 + ((int8_t)tile << 4) + (off_y << 1);
    }

    c1 = gbx_read_byte(ctx, addr + 0);
    c2 = gbx_read_byte(ctx, addr + 1);

    cnum = ((c1 >> bit) & 1) | (((c2 >> bit) << 1) & 2);
    if (ctx->cgb_enabled) {
        // read the BG map attributes from VRAM bank 1
        if (ctx->video.lcdc & LCDC_BG_CODE)
            attr = ctx->mem.vram[VRAM_BANK_SIZE + 0x1C00 + mappos];
        else
            attr = ctx->mem.vram[VRAM_BANK_SIZE + 0x1800 + mappos];
        color = ctx->video.bcpd_rgb[cnum | ((attr & 0x07) << 2)];
    }
    else
        color = ctx->video.bgp_rgb[cnum];

    ctx->fb[y * GBX_LCD_XRES + x] = color;
}

// -----------------------------------------------------------------------------
INLINE void set_stat_mode(gbx_context_t *ctx, int mode)
{
    assert(mode >= 0 && mode <= 3);
    ctx->video.stat = (ctx->video.stat & ~STAT_MODE) | mode;
    log_spew("LCD STAT mode changing to %d\n", mode);
}

// -----------------------------------------------------------------------------
INLINE void check_coincidence(gbx_context_t *ctx)
{
    // fire an interrupt when LY == LYC becomes true and interrupt enabled
    if (ctx->video.lcd_y == ctx->video.lyc && ctx->video.stat & STAT_INT_LYC)
        gbx_req_interrupt(ctx, INT_LCDSTAT);

    // this is only called when Y transitions, so rather than set the STAT LYC
    // flag here, it is computed on demand when the STAT register is read
}

// -----------------------------------------------------------------------------
INLINE void transition_to_search(gbx_context_t *ctx)
{
    ctx->video.state = VIDEO_STATE_SEARCH;
    ctx->video.cycle = 0;
    ctx->video.lcd_x = 0;

    // check for OAM STAT interrupt
    set_stat_mode(ctx, MODE_SEARCH);
    if (ctx->video.stat & STAT_INT_OAM)
        gbx_req_interrupt(ctx, INT_LCDSTAT);
}

// -----------------------------------------------------------------------------
INLINE void transition_to_transfer(gbx_context_t *ctx)
{
    ctx->video.state = VIDEO_STATE_TRANSFER;
    ctx->video.cycle = 0;

    // no SEARCH STAT interrupt
    set_stat_mode(ctx, MODE_TRANSFER);
}

// -----------------------------------------------------------------------------
INLINE void transition_to_hblank(gbx_context_t *ctx)
{
    ctx->video.state = VIDEO_STATE_HBLANK;
    ctx->video.cycle = 0;

    // check for HBLANK STAT interrupt
    set_stat_mode(ctx, MODE_HBLANK);
    if (ctx->video.stat & STAT_INT_HBLANK)
        gbx_req_interrupt(ctx, INT_LCDSTAT);
}

// -----------------------------------------------------------------------------
INLINE void transition_to_vblank(gbx_context_t *ctx)
{
    ctx->video.state = VIDEO_STATE_VBLANK;
    ctx->video.cycle = 0;

    render_sprites(ctx);
    ext_video_sync(ctx->userdata);
    gbx_req_interrupt(ctx, INT_VBLANK);

    // check for VBLANK STAT interrupt
    set_stat_mode(ctx, MODE_VBLANK);
    if (ctx->video.stat & STAT_INT_VBLANK)
        gbx_req_interrupt(ctx, INT_LCDSTAT);
}

// -----------------------------------------------------------------------------
void video_update_cycles(gbx_context_t *ctx, long cycles)
{
    long i;

    // if in double speed mode, halve the number of LCD clock cycles
    if (ctx->key1 & KEY1_SPEED)
        cycles >>= 1;

    if (!(ctx->video.lcdc & LCDC_LCD_EN))
        return;

    // drive the LCD for each cycle elapsed, inefficient but accurate
    for (i = 0; i < cycles; i++) {

        switch (ctx->video.state) {
        case VIDEO_STATE_SEARCH:
            // check for OAM search completion, transition to data transfer
            if (++ctx->video.cycle >= SEARCH_CYCLES)
                transition_to_transfer(ctx);
            break;
        case VIDEO_STATE_TRANSFER:
            // render each pixel of the current scanline
            if (ctx->video.lcd_x < GBX_LCD_XRES) {
                render_bg_pixel(ctx, ctx->video.lcd_x, ctx->video.lcd_y);
                ++ctx->video.lcd_x;
            }

            // check for data transfer completion, transition to h-blank
            if (++ctx->video.cycle >= TRANSFER_CYCLES)
                transition_to_hblank(ctx);
            break;
        case VIDEO_STATE_HBLANK:
            // check for h-blank completion, transition to v-blank or search
            if (++ctx->video.cycle >= HBLANK_CYCLES) {
                if (++ctx->video.lcd_y >= GBX_LCD_YRES)
                    transition_to_vblank(ctx);
                else
                    transition_to_search(ctx);

                // check for coincidence interrupt each time LY changes
                check_coincidence(ctx);
            }
            break;
        case VIDEO_STATE_VBLANK:
            // check for v-blank completion, transition to oam search
            if (++ctx->video.cycle >= HORIZONTAL_CYCLES) {
                if (++ctx->video.lcd_y >= TOTAL_SCANLINES) {
                    transition_to_search(ctx);
                    ctx->video.lcd_y = 0;
                }

                // check for coincidence interrupt each time LY changes
                check_coincidence(ctx);
                ctx->video.cycle = 0;
            }
            break;
        }
    }
}
