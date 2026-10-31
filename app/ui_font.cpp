/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 * Copyright (c) 2026 Stafford Lumsden
 */

#include "ui_font.hpp"

#include <algorithm>
#include <cstdint>
#include <new>

#include "esp_heap_caps.h"
#include "fonts/pixel_fonts.h"
#include "packed_font.hpp"
#include "resources.h"
#include "widgets/fonts.hpp"

namespace {

/* LVGL needs raster masks, while the Console PixelFonts use a compact 1-bit
 * packed format. Rasterize each ASCII glyph once into the Console scale used
 * by the corresponding UI role, then hand LVGL a stable A8 bitmap. */
class PixelFont {
public:
    PixelFont(const cyd_fonts::Font &source, int scale, int line_height, int base_line)
        : source_(source), scale_(scale) {
        for (uint16_t codepoint = source.first; codepoint <= source.last; codepoint++) {
            max_height_ = std::max<int>(max_height_,
                source.glyphs[codepoint - source.first].height * scale);
        }
        font_.get_glyph_dsc = get_glyph_dsc_cb;
        font_.get_glyph_bitmap = get_glyph_bitmap_cb;
        font_.line_height = line_height;
        font_.base_line = base_line;
        font_.subpx = LV_FONT_SUBPX_NONE;
        font_.kerning = LV_FONT_KERNING_NONE;
        font_.static_bitmap = 1;
        font_.dsc = this;
    }

    const lv_font_t *font() const { return &font_; }
    int max_height() const { return max_height_; }

private:
    struct Cache {
        uint8_t *bitmap = nullptr;
    };

    static bool get_glyph_dsc_cb(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                                 uint32_t letter, uint32_t) {
        auto *self = static_cast<const PixelFont *>(font->dsc);
        if (letter < self->source_.first || letter > self->source_.last) return false;
        const auto &glyph = self->source_.glyphs[letter - self->source_.first];

        out->adv_w = uint16_t((glyph.advance + 1) * self->scale_);
        out->box_w = uint16_t(glyph.width * self->scale_);
        out->box_h = uint16_t(glyph.height * self->scale_);
        out->ofs_x = int16_t(glyph.x * self->scale_);
        /* PixelFonts place every glyph bottom on the baseline. LVGL's
         * line-height/base-line pair supplies that baseline. */
        out->ofs_y = 0;
        out->stride = out->box_w;
        out->format = LV_FONT_GLYPH_FORMAT_A8;
        out->is_placeholder = false;
        out->gid.index = letter - self->source_.first;
        return true;
    }

    static const void *get_glyph_bitmap_cb(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *) {
        auto *self = const_cast<PixelFont *>(
            static_cast<const PixelFont *>(dsc->resolved_font->dsc));
        const uint32_t index = dsc->gid.index;
        const auto &glyph = self->source_.glyphs[index];
        if (!glyph.width || !glyph.height) return nullptr;
        if (self->cache_[index].bitmap) return self->cache_[index].bitmap;

        const uint32_t width = glyph.width * self->scale_;
        const uint32_t height = glyph.height * self->scale_;
        auto *bitmap = static_cast<uint8_t *>(heap_caps_malloc(width * height, MALLOC_CAP_SPIRAM));
        if (!bitmap) return nullptr;

        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                const uint32_t source_x = x / self->scale_;
                const uint32_t source_y = y / self->scale_;
                const uint32_t bit = source_y * glyph.width + source_x;
                const bool set = self->source_.bitmap[glyph.offset + bit / 8] &
                                 (0x80u >> (bit % 8));
                bitmap[y * width + x] = set ? 0xff : 0x00;
            }
        }
        self->cache_[index].bitmap = bitmap;
        return bitmap;
    }

    const cyd_fonts::Font &source_;
    int scale_;
    int max_height_ = 0;
    lv_font_t font_{};
    mutable Cache cache_[95]{};
};

struct UiFonts {
    PixelFont body_pixel{cyd_fonts::pixelfont_7x5_thin, 2, 16, 2};
    PixelFont caption_pixel{cyd_fonts::pixelfont_11x5_thin, 2, 16, 2};
    PixelFont heading_pixel{cyd_fonts::pixelfont_13x7_semibold, 2, 18, 2};
    PixelFont title_pixel{cyd_fonts::pixelfont_13x7_semibold, 3, 26, 2};
    PackedFont body_jp{noto_sans_jp_24};
    PackedFont title_jp{noto_sans_jp_38};
    lv_font_t body{};
    lv_font_t caption{};
    lv_font_t heading{};
    lv_font_t title{};
};

UiFonts *s_fonts = nullptr;

/* Japanese filenames and tags stay supported through the existing subset font,
 * while Latin text uses the Console pixel face. */
lv_font_t chain(const PixelFont &latin, const PackedFont &japanese) {
    lv_font_t font = *latin.font();
    font.fallback = japanese.font();
    font.base_line = std::max(font.base_line, japanese.max_descent());
    const int32_t line_height = std::max({
        font.line_height,
        japanese.max_ascent() + japanese.max_descent(),
        font.base_line + latin.max_height(),
    });
    font.line_height = line_height;
    return font;
}

}  // namespace

void ui_font_init() {
    if (s_fonts) return;
    void *memory = heap_caps_malloc(sizeof(UiFonts), MALLOC_CAP_SPIRAM);
    if (!memory) return;

    s_fonts = new (memory) UiFonts();
    s_fonts->body = chain(s_fonts->body_pixel, s_fonts->body_jp);
    s_fonts->caption = chain(s_fonts->caption_pixel, s_fonts->body_jp);
    s_fonts->heading = chain(s_fonts->heading_pixel, s_fonts->body_jp);
    s_fonts->title = chain(s_fonts->title_pixel, s_fonts->title_jp);
}

const lv_font_t *lv_widgets_font(lv_widgets_font_role_t role) {
    if (!s_fonts) return nullptr;
    switch (role) {
    case LV_WIDGETS_FONT_BODY:
        return &s_fonts->body;
    case LV_WIDGETS_FONT_CAPTION:
    case LV_WIDGETS_FONT_BUTTON:
        return &s_fonts->caption;
    case LV_WIDGETS_FONT_HEADING:
        return &s_fonts->heading;
    case LV_WIDGETS_FONT_TITLE:
        return &s_fonts->title;
    case LV_WIDGETS_FONT_ICON:
        return &lv_font_montserrat_38;
    default:
        return nullptr;
    }
}
