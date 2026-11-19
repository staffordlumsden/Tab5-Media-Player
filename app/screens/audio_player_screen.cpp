/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hiroki Kawakami
 */

#include "audio_player_screen.hpp"
#include "media_player.hpp"
#include "screens/image_object.hpp"
#include "screens/media_controls.hpp"
#include "screens/player_layout.hpp"
#include "screens/video_player/info_panel.hpp"
#include "screens/video_player/settings_panel.hpp"
#include "settings.hpp"
#include "resources.h"
#include "bsp.h"
#include "esp_timer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr uint32_t kRefreshPeriodMs = 500;
static constexpr uint32_t kResolveTimeoutMs = 700;
static constexpr int64_t kPrefetchDelayUs = 2000000;
static constexpr int64_t kPrevTrackUs = 3000000;
static constexpr int64_t kFollowTrackUs = 30000000;
static constexpr int32_t kSeekRange = 1000;
static constexpr int32_t kPad = 24;
static constexpr int32_t kGap = 24;
static constexpr int32_t kPlaylistHeaderHeight = 64;
static constexpr int32_t kPlaylistRowHeight = 72;
static constexpr int32_t kThumbSide = 56;
static constexpr ImageSize kThumbBox = { kThumbSide, kThumbSide };
static constexpr int32_t kThumbRadius = 6;
static constexpr int32_t kLockPad = 32;
static constexpr int32_t kLockGap = 24;
static constexpr int32_t kLockButtonHeight = 72;
static constexpr int32_t kLockArtworkRadius = 24;
static constexpr std::size_t kRowLookahead = 2;
static constexpr ImageSize kArtworkBox = { kPlayerArtworkSide, kPlayerArtworkSide };
/* LVGL draws through framebuffer 0 only. */
static constexpr int kArtworkFramebuffer = 1;

static constexpr const char *kTitle = "Music Player";

static int32_t body_line_height() {
    return lv_font_get_line_height(lv_widgets_resolved_font(LV_WIDGETS_FONT_BODY));
}

static std::string directory_name(const std::string &path) {
    const std::size_t slash = path.rfind('/');
    if (slash == std::string::npos || slash == 0) return {};
    const std::size_t start = path.rfind('/', slash - 1);
    return path.substr(start == std::string::npos ? 0 : start + 1, slash - start - 1);
}

static AudioPlayerScreen *s_active;

static std::string format_audio(CodecId codec, unsigned rate, unsigned channel_count) {
    if (codec == CodecId::None) return {};
    char text[64];
    const char *name = codec_name(codec);
    const char *channels = channel_count == 1   ? "mono"
                         : channel_count == 2   ? "stereo"
                                                : nullptr;
    if (!rate) {
        snprintf(text, sizeof(text), "%s", name);
    } else if (channels) {
        snprintf(text, sizeof(text), "%s  %.1f kHz  %s", name, rate / 1000.0, channels);
    } else {
        snprintf(text, sizeof(text), "%s  %.1f kHz  %uch", name, rate / 1000.0, channel_count);
    }
    return text;
}

static std::string format_summary(const MediaSummary &summary) {
    if (!summary.valid) return {};
    return format_audio(summary.audio.codec, summary.audio.sample_rate, summary.audio.channels);
}

static std::string format_names(const std::string &artist, const std::string &album) {
    if (artist.empty()) return album;
    if (album.empty()) return artist;
    return artist + " \u00b7 " + album;
}

static std::string format_tags(const MediaTags &tags) {
    return format_names(tags.artist, tags.album);
}

AudioPlayerScreen::AudioPlayerScreen(std::shared_ptr<Playlist> playlist)
    : playlist_(std::move(playlist)), repeat_(settings_audio_repeat()) {
    playlist_->setShuffled(settings_audio_shuffle());
}

void AudioPlayerScreen::build() {
    lv_obj_add_event_fn(root_, LV_EVENT_SIZE_CHANGED, [this](lv_event_t *) {
        if (player_layout_landscape(root_) != landscape_) relayout();
    });
    buildContents();
}

void AudioPlayerScreen::relayout() {
    lv_async_call([this] {
        if (s_active == this) buildContents();
    });
}

void AudioPlayerScreen::buildContents() {
    if (lv_group_t *group = lv_group_get_default()) lv_group_focus_freeze(group, false);
    segment_ = nullptr;
    pane_box_ = nullptr;
    pane_view_ = nullptr;
    playlist_list_ = nullptr;
    playlist_count_ = nullptr;
    play_label_ = nullptr;
    next_button_ = nullptr;
    artwork_ = nullptr;
    artwork_icon_ = nullptr;
    artwork_image_ = nullptr;
    lock_overlay_ = nullptr;
    lock_artwork_ = nullptr;
    lock_artwork_icon_ = nullptr;
    lock_artwork_image_ = nullptr;
    lock_title_ = nullptr;
    lock_subtitle_ = nullptr;
    lock_unlock_button_ = nullptr;
    lock_screen_off_button_ = nullptr;
    lock_wake_shield_ = nullptr;
    title_label_ = nullptr;
    repeat_button_ = nullptr;
    repeat_label_ = nullptr;
    shuffle_button_ = nullptr;
    seek_ = nullptr;
    elapsed_label_ = nullptr;
    total_label_ = nullptr;
    subtitle_label_ = nullptr;
    volume_label_ = nullptr;
    volume_slider_ = nullptr;
    scrubbing_ = false;
    shown_elapsed_s_ = -2;
    shown_total_s_ = -2;
    shown_title_.clear();
    shown_subtitle_.clear();

    landscape_ = player_layout_landscape(root_);
    const PlayerLayout layout = player_layout_build(
        root_, kTitle, [this] { back(); },
        { TABLER_PHOTO, TABLER_PLAYLIST, TABLER_INFO_CIRCLE, TABLER_ADJUSTMENTS_HORIZONTAL },
        [this](int index) {
            if ((int)pane_ != index) showPane((Pane)index);
        });
    navigation_ = layout.navigation;
    navigation_title_ = layout.back_label;
    contents_ = layout.controls;
    segment_ = layout.segment;
    pane_box_ = layout.pane;
    title_label_ = layout.title;
    subtitle_label_ = layout.subtitle;
    shown_title_ = currentTitle();
    player_scrolling_text_set(title_label_, shown_title_);

    buildArtwork(pane_box_);
    buildSeekRow(layout.controls);
    buildTransport(layout.controls);
    buildVolumeRow(layout.controls);
    showPane(pane_);

    setPlayIcon(playing_);
    setRepeatMode(repeat_);
    setShuffle(playlist_->shuffled());
    refresh();
    if (lock_state_ != LockState::Unlocked) buildLockOverlay();
}

void AudioPlayerScreen::showPane(Pane pane) {
    pane_ = pane;
    player_segment_set_active(segment_, (int)pane_);
    if (pane_view_) {
        lv_obj_delete(pane_view_);
        pane_view_ = nullptr;
        playlist_list_ = nullptr;
        playlist_count_ = nullptr;
    }
    if (rows_token_) media_cache_cancel(rows_token_);
    requested_first_ = SIZE_MAX;
    lv_obj_set_hidden(artwork_, pane != Pane::Artwork);
    if (pane == Pane::Artwork) return;

    pane_view_ = player_pane_view_create(pane_box_);

    switch (pane) {
    case Pane::Playlist:
        buildPlaylist();
        break;
    case Pane::Info:
        lv_setting_page_style(pane_view_);
        showInfo();
        break;
    case Pane::Settings:
        lv_setting_page_style(pane_view_);
        player_settings_sections_build(pane_view_, PlayerSettingsDisplay | PlayerSettingsSound,
                                       nullptr, false);
        break;
    case Pane::Artwork:
        break;
    }
}

void AudioPlayerScreen::showInfo() {
    const MediaSummary summary = awaiting_start_ ? MediaSummary{} : player_media_summary();
    player_info_sections_show(pane_view_, name(), summary, nullptr);
}

void AudioPlayerScreen::buildPlaylist() {
    lv_obj_t *header = lv_container_create(pane_view_, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(header, kPlaylistHeaderHeight);
    lv_obj_set_style_pad_hor(header, kPad, 0);
    lv_obj_set_style_pad_column(header, kGap, 0);

    lv_obj_t *name = lv_label_create(header);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_height(name, body_line_height());
    lv_obj_set_font_role(name, LV_WIDGETS_FONT_BODY);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(name, directory_name(path()).c_str());

    playlist_count_ = lv_label_create(header);
    lv_obj_set_font_role(playlist_count_, LV_WIDGETS_FONT_BODY);
    lv_obj_set_style_text_color(playlist_count_, ui_theme::color(player_subtitle_color()), 0);
    updatePlaylistCount();

    lv_hor_separator_create(pane_view_);
    shown_order_ = playlist_->orderVersion();
    shown_position_ = playlist_->position();
    playlist_list_ = lv_list_create(pane_view_, this);
    lv_obj_add_event_fn(playlist_list_, LV_EVENT_SCROLL, [this](lv_event_t *) {
        if (lv_indev_active()) list_scrolled_us_ = esp_timer_get_time();
        requestRows();
    });
    lv_list_show_row(playlist_list_, playlist_->position());
    /* A jump made while a tap is being handled reads as the user's scroll. */
    list_scrolled_us_ = esp_timer_get_time() - kFollowTrackUs;
    requestRows();
}

void AudioPlayerScreen::syncPlaylist() {
    if (!playlist_list_) return;
    updatePlaylistCount();
    const std::size_t position = playlist_->position();
    if (playlist_->orderVersion() != shown_order_) {
        shown_order_ = playlist_->orderVersion();
        shown_position_ = position;
        lv_list_reload_data(playlist_list_, true);
        lv_list_show_row(playlist_list_, position);
        list_scrolled_us_ = esp_timer_get_time() - kFollowTrackUs;
        requested_first_ = SIZE_MAX;
        requestRows();
        return;
    }
    if (position == shown_position_) return;
    const std::size_t previous = shown_position_;
    shown_position_ = position;
    rebindRow(previous);
    rebindRow(position);
    if (esp_timer_get_time() - list_scrolled_us_ >= kFollowTrackUs) {
        lv_list_show_row(playlist_list_, position, LV_ANIM_ON);
    }
}

void AudioPlayerScreen::rebindRow(std::size_t index) {
    if (lv_obj_t *row = lv_list_row_for_index(playlist_list_, index)) bindRow(row, index);
}

void AudioPlayerScreen::rebindPath(const std::string &path) {
    if (!playlist_list_) return;
    for (std::size_t i = 0; i < playlist_->size(); i++) {
        if (playlist_->at(playlist_->orderAt(i)).path != path) continue;
        rebindRow(i);
        return;
    }
}

void AudioPlayerScreen::updatePlaylistCount() {
    lv_label_set_text_fmt(playlist_count_, "%u/%u", (unsigned)playlist_->position() + 1,
                          (unsigned)playlist_->size());
}

bool AudioPlayerScreen::keepVisible(const char *path, void *ctx) {
    for (const std::string &kept : static_cast<AudioPlayerScreen *>(ctx)->visible_paths_) {
        if (kept == path) return true;
    }
    return false;
}

void AudioPlayerScreen::requestRows() {
    std::size_t first = lv_list_first_visible_row(playlist_list_);
    if (first == SIZE_MAX) first = 0;
    if (first == requested_first_) return;
    requested_first_ = first;

    const int32_t height = lv_obj_get_height(playlist_list_);
    const std::size_t rows = (std::size_t)(height / kPlaylistRowHeight) + 1;
    const std::size_t start = first > kRowLookahead ? first - kRowLookahead : 0;
    const std::size_t end = std::min(playlist_->size(), first + rows + kRowLookahead);
    visible_paths_.clear();
    for (std::size_t i = start; i < end; i++) {
        visible_paths_.push_back(playlist_->at(playlist_->orderAt(i)).path);
    }
    media_cache_retain(rows_token_, keepVisible, this);

    /* A request for what the cache already holds still completes, and every
     * completion reaches metaReady, so only the missing parts are asked for. */
    for (const std::string &row : visible_paths_) {
        if (media_cache_lookup(row)) continue;
        media_cache_request(row, MetaWantInfo, {}, MetaPriority::Visible, rows_token_);
    }
    for (const std::string &row : visible_paths_) {
        const std::shared_ptr<const MediaEntry> meta = media_cache_lookup(row);
        if (meta && meta->cover_scanned && !meta->has_cover) continue;
        if (media_cache_thumbnail(row, kThumbBox)) continue;
        media_cache_request(row, MetaWantInfo | MetaWantThumbnail, kThumbBox,
                            MetaPriority::Visible, rows_token_);
    }
}

void AudioPlayerScreen::buildArtwork(lv_obj_t *pane) {
    artwork_ = player_artwork_create(pane);
    resetArtwork();
    applyArtwork();
}

void AudioPlayerScreen::resetArtwork() {
    if (!artwork_) return;
    if (artwork_image_) {
        lv_obj_delete(artwork_image_);
        artwork_image_ = nullptr;
    }
    if (artwork_icon_) return;

    artwork_icon_ = lv_label_create(artwork_);
    lv_label_set_text(artwork_icon_, TABLER_MUSIC);
    lv_obj_set_style_text_font(artwork_icon_, &icon_120, 0);
    lv_obj_set_style_text_color(artwork_icon_, ui_theme::color(player_artwork_icon_color()), 0);
    lv_obj_center(artwork_icon_);
}

void AudioPlayerScreen::applyArtwork() {
    if (!artwork_ || artwork_image_) return;
    auto *buffer = static_cast<uint8_t *>(bsp_display_get_frame_buffer(kArtworkFramebuffer));
    const bool rgb888 = bsp_display_get_pixel_format() == BSP_PIXEL_FORMAT_RGB888;
    if (!cover_size_.valid()) {
        const ImageSize box = kArtworkBox;
        const bsp_size_t panel = bsp_display_get_size();
        const std::size_t bytes = (std::size_t)panel.width * panel.height * (rgb888 ? 3 : 2);
        if (!media_cache_read_image(path(), box, buffer, bytes, &cover_size_)) return;
    }

    artwork_image_ = image_object_create(artwork_, buffer, cover_size_, rgb888);
    if (!artwork_image_) {
        cover_size_ = {};
        return;
    }
    if (artwork_icon_) {
        lv_obj_delete(artwork_icon_);
        artwork_icon_ = nullptr;
    }
}

void AudioPlayerScreen::lockPlayback() {
    if (lock_state_ != LockState::Unlocked) return;
    lock_state_ = LockState::Locked;
    buildLockOverlay();
}

void AudioPlayerScreen::unlockPlayback() {
    if (lock_state_ == LockState::Unlocked) return;
    lock_state_ = LockState::Unlocked;
    bsp_display_set_power(BSP_DISPLAY_POWER_ON);
    bsp_display_set_brightness(settings_display_brightness());
    if (lv_group_t *group = lv_group_get_default()) lv_group_focus_freeze(group, false);

    /* The event callback belongs to the overlay being removed. Rebuild on the
     * next LVGL turn so the active callback is never deleted mid-dispatch. */
    lv_async_call([this] {
        if (s_active == this) buildContents();
    });
}

void AudioPlayerScreen::configureLockKeyboard(bool screen_off) {
    lv_group_t *group = lv_group_get_default();
    lv_obj_t *target = screen_off ? lock_wake_shield_ : lock_unlock_button_;
    if (!group || !target) return;

    /* Keep every underlying screen's membership intact. Freezing focus confines
     * Tab/arrow navigation to the lock target without emptying the shared group. */
    lv_group_focus_freeze(group, false);
    lv_group_remove_obj(target);
    lv_group_add_obj(group, target);
    lv_group_focus_obj(target);
    lv_group_set_editing(group, false);
    lv_group_focus_freeze(group, true);
}

void AudioPlayerScreen::buildLockOverlay() {
    if (lock_overlay_ || lock_state_ == LockState::Unlocked) return;

    const bool landscape = player_layout_landscape(root_);
    const lv_color_t background = ui_theme::color(ui_theme::colors().bg);
    const lv_color_t surface = ui_theme::color(ui_theme::colors().surface_pressed);
    const lv_color_t foreground = ui_theme::color(ui_theme::colors().text);
    const lv_color_t muted = ui_theme::color(ui_theme::colors().muted);

    lock_overlay_ = lv_container_create(root_, background);
    lv_obj_set_floating(lock_overlay_, true);
    /* The landscape player reserves right padding on the root. Percent sizing
     * uses that padded content box and would leave a visible edge exposed. */
    lv_obj_set_size(lock_overlay_, lv_obj_get_width(root_), lv_obj_get_height(root_));
    lv_obj_set_pos(lock_overlay_, 0, 0);
    lv_obj_set_scrollable(lock_overlay_, false);
    lv_obj_set_clickable(lock_overlay_, true);
    lv_obj_set_flex_flow(lock_overlay_, landscape ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(lock_overlay_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(lock_overlay_, kLockPad, 0);
    lv_obj_set_style_pad_row(lock_overlay_, kLockGap, 0);
    lv_obj_set_style_pad_column(lock_overlay_, kLockGap * 2, 0);
    lv_obj_set_style_text_color(lock_overlay_, foreground, 0);
    lv_obj_move_foreground(lock_overlay_);

    lock_artwork_ = lv_container_create(lock_overlay_, surface);
    lv_obj_set_size(lock_artwork_, kPlayerArtworkSide, kPlayerArtworkSide);
    lv_obj_set_style_radius(lock_artwork_, kLockArtworkRadius, 0);
    lv_obj_set_style_clip_corner(lock_artwork_, true, 0);
    lv_obj_set_scrollable(lock_artwork_, false);
    resetLockArtwork();
    applyLockArtwork();

    lv_obj_t *details = lv_container_create(lock_overlay_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(details, landscape ? LV_SIZE_CONTENT : lv_pct(100),
                    landscape ? lv_pct(100) : LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(details, 1);
    lv_obj_set_flex_align(details, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(details, kLockGap, 0);

    lv_obj_t *heading = lv_container_create(details, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(heading, LV_SIZE_CONTENT, 48);
    lv_obj_set_style_pad_column(heading, 16, 0);
    lv_obj_t *lock_icon = lv_label_create(heading);
    lv_obj_set_style_text_font(lock_icon, &icon_36, 0);
    lv_label_set_text(lock_icon, TABLER_LOCK);
    lv_obj_t *locked = lv_label_create(heading);
    lv_obj_set_font_role(locked, LV_WIDGETS_FONT_HEADING);
    lv_label_set_text(locked, "PLAYBACK LOCKED");

    lock_title_ = lv_label_create(details);
    lv_obj_set_width(lock_title_, lv_pct(100));
    lv_obj_set_height(lock_title_,
                      lv_font_get_line_height(lv_widgets_resolved_font(LV_WIDGETS_FONT_TITLE)));
    lv_obj_set_font_role(lock_title_, LV_WIDGETS_FONT_TITLE);
    lv_obj_set_style_text_align(lock_title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lock_title_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    lock_subtitle_ = lv_label_create(details);
    lv_obj_set_width(lock_subtitle_, lv_pct(100));
    lv_obj_set_height(lock_subtitle_,
                      lv_font_get_line_height(lv_widgets_resolved_font(LV_WIDGETS_FONT_BODY)));
    lv_obj_set_font_role(lock_subtitle_, LV_WIDGETS_FONT_BODY);
    lv_obj_set_style_text_align(lock_subtitle_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lock_subtitle_, muted, 0);
    lv_label_set_long_mode(lock_subtitle_, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    lv_obj_t *prompt = lv_label_create(details);
    lv_obj_set_width(prompt, lv_pct(100));
    lv_obj_set_font_role(prompt, LV_WIDGETS_FONT_CAPTION);
    lv_obj_set_style_text_align(prompt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(prompt, muted, 0);
    lv_label_set_long_mode(prompt, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(prompt, "PLAYBACK CONTINUES  ·  WAKE FIRST, THEN UNLOCK");

    lv_obj_t *actions = lv_container_create(details, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(actions, lv_pct(100), kLockButtonHeight);
    lv_obj_set_style_pad_column(actions, 16, 0);

    lock_unlock_button_ = lv_button_create(actions, LV_BUTTON_STYLE_PRIMARY);
    lv_obj_set_height(lock_unlock_button_, kLockButtonHeight);
    lv_obj_set_flex_grow(lock_unlock_button_, 1);
    lv_button_set_text(lock_unlock_button_, "UNLOCK");
    lv_obj_add_event_fn(lock_unlock_button_, LV_EVENT_CLICKED,
                        [this](lv_event_t *) { unlockPlayback(); });

    lock_screen_off_button_ = lv_button_create(actions, LV_BUTTON_STYLE_SECONDARY);
    lv_obj_set_height(lock_screen_off_button_, kLockButtonHeight);
    lv_obj_set_flex_grow(lock_screen_off_button_, 1);
    lv_button_set_text(lock_screen_off_button_, "SCREEN OFF");
    lv_obj_add_event_fn(lock_screen_off_button_, LV_EVENT_CLICKED,
                        [this](lv_event_t *) { enterScreenOff(); });

    updateLockText();
    if (lock_state_ == LockState::ScreenOff) {
        buildWakeShield();
    } else {
        configureLockKeyboard(false);
    }
}

void AudioPlayerScreen::resetLockArtwork() {
    if (!lock_artwork_) return;
    if (lock_artwork_image_) {
        lv_obj_delete(lock_artwork_image_);
        lock_artwork_image_ = nullptr;
    }
    if (lock_artwork_icon_) return;

    lock_artwork_icon_ = lv_label_create(lock_artwork_);
    lv_label_set_text(lock_artwork_icon_, TABLER_MUSIC);
    lv_obj_set_style_text_font(lock_artwork_icon_, &icon_120, 0);
    lv_obj_set_style_text_color(lock_artwork_icon_,
                                ui_theme::color(player_artwork_icon_color()), 0);
    lv_obj_center(lock_artwork_icon_);
}

void AudioPlayerScreen::applyLockArtwork() {
    if (!lock_artwork_ || lock_artwork_image_ || !cover_size_.valid()) return;
    auto *buffer = static_cast<uint8_t *>(bsp_display_get_frame_buffer(kArtworkFramebuffer));
    const bool rgb888 = bsp_display_get_pixel_format() == BSP_PIXEL_FORMAT_RGB888;
    lock_artwork_image_ = image_object_create(lock_artwork_, buffer, cover_size_, rgb888);
    if (!lock_artwork_image_) return;
    if (lock_artwork_icon_) {
        lv_obj_delete(lock_artwork_icon_);
        lock_artwork_icon_ = nullptr;
    }
}

void AudioPlayerScreen::updateLockText() {
    if (lock_title_ && strcmp(lv_label_get_text(lock_title_), shown_title_.c_str()) != 0) {
        player_scrolling_text_set(lock_title_, shown_title_);
    }
    if (lock_subtitle_ && strcmp(lv_label_get_text(lock_subtitle_), shown_subtitle_.c_str()) != 0) {
        player_scrolling_text_set(lock_subtitle_, shown_subtitle_);
    }
}

void AudioPlayerScreen::buildWakeShield() {
    if (!lock_overlay_ || lock_wake_shield_) return;
    /* Keep the wake target outside the overlay's padded flex layout so it is a
     * true full-panel shield, not another album-art/details row. */
    lock_wake_shield_ = lv_container_create(root_, lv_color_black());
    lv_obj_set_floating(lock_wake_shield_, true);
    lv_obj_set_size(lock_wake_shield_, lv_obj_get_width(root_), lv_obj_get_height(root_));
    lv_obj_set_pos(lock_wake_shield_, 0, 0);
    lv_obj_set_scrollable(lock_wake_shield_, false);
    lv_obj_set_clickable(lock_wake_shield_, true);
    lv_obj_move_foreground(lock_wake_shield_);

    lv_obj_add_event_fn(lock_wake_shield_, LV_EVENT_PRESSED, [this](lv_event_t *event) {
        if (lock_state_ != LockState::ScreenOff) return;
        wakeFromScreenOff();
        lv_event_stop_processing(event);
    });
    lv_obj_add_event_fn(lock_wake_shield_, LV_EVENT_RELEASED, [this](lv_event_t *event) {
        if (lock_state_ != LockState::Locked) return;
        finishTouchWake();
        lv_event_stop_processing(event);
    });
    lv_obj_add_event_fn(lock_wake_shield_, LV_EVENT_PRESS_LOST, [this](lv_event_t *event) {
        if (lock_state_ != LockState::Locked) return;
        finishTouchWake();
        lv_event_stop_processing(event);
    });
    lv_obj_add_event_fn(lock_wake_shield_, LV_EVENT_KEY, [this](lv_event_t *event) {
        if (lock_state_ != LockState::ScreenOff) return;
        wakeFromScreenOff();
        finishTouchWake();
        lv_event_stop_processing(event);
    });
    configureLockKeyboard(true);
}

void AudioPlayerScreen::enterScreenOff() {
    if (lock_state_ != LockState::Locked) return;
    lock_state_ = LockState::ScreenOff;
    buildWakeShield();
    bsp_display_set_brightness(0);
    bsp_display_set_power(BSP_DISPLAY_POWER_SLEEP);
}

void AudioPlayerScreen::wakeFromScreenOff() {
    if (lock_state_ != LockState::ScreenOff) return;
    lock_state_ = LockState::Locked;
    bsp_display_set_power(BSP_DISPLAY_POWER_ON);
    bsp_display_set_brightness(settings_display_brightness());
}

void AudioPlayerScreen::finishTouchWake() {
    /* A touch release still belongs to the shield. Remove it asynchronously so
     * that release cannot land on UNLOCK and collapse the two-step contract. */
    lv_async_call([this] {
        if (s_active != this || lock_state_ != LockState::Locked || !lock_wake_shield_) return;
        lv_obj_delete(lock_wake_shield_);
        lock_wake_shield_ = nullptr;
        configureLockKeyboard(false);
    });
}

std::string AudioPlayerScreen::currentTitle() const {
    if (meta_ && meta_->title[0]) return meta_->title;
    return name();
}

void AudioPlayerScreen::requestMeta() {
    meta_ = media_cache_resolve(path(), MetaWantInfo, {}, kResolveTimeoutMs);
    media_cache_request(path(), MetaWantInfo | MetaWantImage, kArtworkBox,
                        MetaPriority::Blocking, token_);
    prefetch_after_us_ = esp_timer_get_time() + kPrefetchDelayUs;
    prefetched_ = false;
}

void AudioPlayerScreen::prefetchNeighbours() {
    if (prefetched_ || prefetch_after_us_ == 0) return;
    if (!playing_ || esp_timer_get_time() < prefetch_after_us_) return;
    prefetched_ = true;

    const std::size_t count = playlist_->size();
    if (count < 2) return;
    const std::size_t next = playlist_->neighbour(1);
    const std::size_t previous = playlist_->neighbour(-1);

    media_cache_request(playlist_->at(next).path, MetaWantInfo | MetaWantImage, kArtworkBox,
                        MetaPriority::Idle, token_);
    if (previous != next) {
        media_cache_request(playlist_->at(previous).path, MetaWantInfo, {}, MetaPriority::Idle,
                            token_);
    }
}

void AudioPlayerScreen::metaReady(const std::string &path) {
    if (!s_active) return;
    s_active->rebindPath(path);
    if (s_active->path() != path) return;
    if (!s_active->meta_) s_active->meta_ = media_cache_lookup(path);
    s_active->applyArtwork();
    s_active->refresh();
}

void AudioPlayerScreen::buildSeekRow(lv_obj_t *controls) {
    const PlayerSeekRow row = player_seek_row_build(controls, kSeekRange);
    elapsed_label_ = row.elapsed;
    seek_ = row.slider;
    total_label_ = row.total;
    lv_obj_add_event_fn(seek_, LV_EVENT_PRESSED, [this](lv_event_t *) { scrubbing_ = true; });
    lv_obj_add_event_fn(seek_, LV_EVENT_VALUE_CHANGED, [this](lv_event_t *) {
        if (!scrubbing_) return;
        const PlayerStatus status = player_status();
        if (status.duration_us <= 0) return;
        setTime(elapsed_label_, &shown_elapsed_s_,
                (int64_t)lv_slider_get_value(seek_) * status.duration_us / kSeekRange);
    });
    lv_obj_add_event_fn(seek_, LV_EVENT_RELEASED, [this](lv_event_t *) {
        scrubbing_ = false;
        const PlayerStatus status = player_status();
        if (!status.seekable || status.duration_us <= 0) return;
        player_seek((int64_t)lv_slider_get_value(seek_) * status.duration_us / kSeekRange);
    });
}

void AudioPlayerScreen::buildTransport(lv_obj_t *controls) {
    const lv_color_t foreground = ui_theme::color(player_foreground_color());
    const PlayerRow row = player_row_build(controls);

    shuffle_button_ = media_toggle_button(row.left, kPlayerIconButton, &icon_36,
                                          TABLER_ARROWS_SHUFFLE, foreground,
                                          ui_theme::color(ui_theme::colors().surface));
    lv_obj_add_event_fn(shuffle_button_, LV_EVENT_CLICKED, [this](lv_event_t *) {
        setShuffle(!playlist_->shuffled());
        settings_set_audio_shuffle(playlist_->shuffled());
        settings_commit();
    });

    lv_obj_t *prev =
        media_icon_button(row.middle, 96, &icon_48, TABLER_PLAYER_TRACK_PREV, foreground);
    lv_obj_add_event_fn(prev, LV_EVENT_CLICKED, [this](lv_event_t *) {
        const PlayerStatus status = player_status();
        if (status.position_us < kPrevTrackUs && advance(-1, true)) return;
        restart(status.state == PlayerState::Playing || status.state == PlayerState::Finished);
    });

    lv_obj_t *play =
        media_icon_button(row.middle, 120, &icon_72, TABLER_PLAYER_PLAY, foreground, &play_label_);
    lv_obj_add_event_fn(play, LV_EVENT_CLICKED, [this](lv_event_t *) {
        if (!awaiting_start_) {
            if (playing_) {
                player_pause();
            } else {
                player_play();
            }
        }
        setPlayIcon(!playing_);
    });

    next_button_ =
        media_icon_button(row.middle, 96, &icon_48, TABLER_PLAYER_TRACK_NEXT, foreground);
    lv_obj_add_event_fn(next_button_, LV_EVENT_CLICKED, [this](lv_event_t *) { advance(1, true); });

    repeat_button_ = media_toggle_button(row.right, kPlayerIconButton, &icon_36, TABLER_REPEAT,
                                         foreground, ui_theme::color(ui_theme::colors().surface),
                                         &repeat_label_);
    lv_obj_add_event_fn(repeat_button_, LV_EVENT_CLICKED, [this](lv_event_t *) {
        switch (repeat_) {
        case RepeatMode::Off: setRepeatMode(RepeatMode::All); break;
        case RepeatMode::All: setRepeatMode(RepeatMode::One); break;
        case RepeatMode::One: setRepeatMode(RepeatMode::Off); break;
        }
        player_set_loop(repeat_ == RepeatMode::One);
        settings_set_audio_repeat(repeat_);
        settings_commit();
    });
}

void AudioPlayerScreen::buildVolumeRow(lv_obj_t *controls) {
    const lv_color_t foreground = ui_theme::color(player_foreground_color());
    const PlayerRow row = player_row_build(controls);
    lv_obj_t *mute = media_icon_button(row.left, kPlayerIconButton, &icon_36, TABLER_VOLUME,
                                       foreground, &volume_label_);
    volume_slider_ = media_slider(row.middle, 100, foreground,
                                  ui_theme::color(player_track_color()));
    lv_obj_set_flex_grow(volume_slider_, 1);
    media_volume_bind(mute, volume_label_, volume_slider_);

    lv_obj_t *lock = media_icon_button(row.right, kPlayerIconButton, &icon_36, TABLER_LOCK,
                                       foreground);
    lv_obj_add_event_fn(lock, LV_EVENT_CLICKED, [this](lv_event_t *) { lockPlayback(); });
}

void AudioPlayerScreen::openCurrent() {
    awaiting_start_ = true;
    scrubbing_ = false;
    cover_size_ = {};
    meta_ = nullptr;
    resetArtwork();
    resetLockArtwork();
    requestMeta();
    player_open(path());
    player_set_loop(repeat_ == RepeatMode::One);

    shown_elapsed_s_ = -2;
    shown_total_s_ = -2;
    if (title_label_) {
        shown_title_ = currentTitle();
        player_scrolling_text_set(title_label_, shown_title_);
    }
    if (subtitle_label_) {
        shown_subtitle_.clear();
        player_scrolling_text_set(subtitle_label_, "");
        lv_obj_set_style_text_color(subtitle_label_, ui_theme::color(player_subtitle_color()), 0);
    }
    syncPlaylist();
    updateTransport();
    setPlayIcon(true);
    refresh();
}

bool AudioPlayerScreen::advance(int delta, bool manual) {
    const std::size_t before = playlist_->index();
    if (!playlist_->step(delta, repeat_)) return false;
    auto_opened_ = !manual;
    if (manual) skips_ = 0;
    if (playlist_->index() == before) {
        restart(true);
        return true;
    }
    openCurrent();
    return true;
}

void AudioPlayerScreen::restart(bool resume) {
    player_restart();
    if (!resume) return;
    player_play();
    setPlayIcon(true);
}

void AudioPlayerScreen::updateTransport() {
    if (!next_button_) return;
    lv_obj_set_state(next_button_, LV_STATE_DISABLED, !playlist_->canStep(1, repeat_));
}

void AudioPlayerScreen::playerStateChanged() {
    if (s_active) s_active->handleState();
}

void AudioPlayerScreen::handleState() {
    prefetchNeighbours();
    const PlayerStatus status = player_status();
    if (awaiting_start_) {
        if (status.state == PlayerState::Paused) {
            awaiting_start_ = false;
            skips_ = 0;
            if (playing_) {
                player_play();
                /* The status still says paused, so refreshing here would flip
                 * the icon back. The Playing transition brings the next one. */
                return;
            }
        }
        if (status.state == PlayerState::Failed) {
            awaiting_start_ = false;
            if (auto_opened_ && ++skips_ < (int)playlist_->size() && advance(1, false)) return;
        }
    } else if (status.state == PlayerState::Finished && advance(1, false)) {
        return;
    }
    refresh();
}

void AudioPlayerScreen::setRepeatMode(RepeatMode mode) {
    repeat_ = mode;
    updateTransport();
    if (!repeat_button_) return;
    lv_label_set_text(repeat_label_, mode == RepeatMode::One ? TABLER_REPEAT_ONCE : TABLER_REPEAT);
    lv_obj_set_state(repeat_button_, LV_STATE_CHECKED, mode != RepeatMode::Off);
}

void AudioPlayerScreen::setShuffle(bool shuffled) {
    playlist_->setShuffled(shuffled);
    syncPlaylist();
    updateTransport();
    if (shuffle_button_) lv_obj_set_state(shuffle_button_, LV_STATE_CHECKED, shuffled);
}

void AudioPlayerScreen::setPlayIcon(bool playing) {
    playing_ = playing;
    if (play_label_) {
        lv_label_set_text(play_label_, playing ? TABLER_PLAYER_PAUSE : TABLER_PLAYER_PLAY);
    }
}

void AudioPlayerScreen::setTime(lv_obj_t *label, int64_t *shown_s, int64_t us) {
    const int64_t seconds = us < 0 ? -1 : us / 1000000;
    if (seconds == *shown_s) return;
    *shown_s = seconds;
    char text[16];
    media_format_time(text, sizeof(text), seconds);
    lv_label_set_text(label, text);
}

void AudioPlayerScreen::refresh() {
    if (!seek_) return;

    /* Until the file the player was told to open is loaded, its status and
     * summary still describe the previous one. */
    const bool ready = !awaiting_start_;
    const PlayerStatus status = player_status();
    const bool playing = ready ? status.state == PlayerState::Playing : playing_;
    if (playing != playing_) setPlayIcon(playing);

    const bool known = ready && status.duration_us > 0 && status.state != PlayerState::Loading;
    setTime(total_label_, &shown_total_s_, known ? status.duration_us : -1);
    if (!scrubbing_) {
        setTime(elapsed_label_, &shown_elapsed_s_, known ? status.position_us : 0);
        const int64_t value = known ? status.position_us * kSeekRange / status.duration_us : 0;
        lv_slider_set_value(seek_, (int32_t)value, LV_ANIM_OFF);
    }

    const MediaSummary summary = ready ? player_media_summary() : MediaSummary{};
    applyArtwork();

    const std::string title = currentTitle();
    if (title != shown_title_) {
        shown_title_ = title;
        player_scrolling_text_set(title_label_, title);
    }

    std::string message;
    if (ready) {
        if (status.state == PlayerState::Failed) message = status.error;
        if (message.empty()) message = status.audio_note;
    }
    const bool failed = !message.empty();
    if (message.empty() && meta_) message = format_names(meta_->artist, meta_->album);
    if (message.empty()) message = format_tags(summary.tags);
    if (message.empty() && meta_) {
        message = format_audio(meta_->audio_codec, meta_->sample_rate, meta_->channels);
    }
    if (message.empty()) message = format_summary(summary);
    /* Until the new file is open the pane keeps the last one's values rather
     * than collapse to the file name and lose its scroll position. */
    if (pane_ == Pane::Info && pane_view_ && ready) showInfo();
    if (message != shown_subtitle_) {
        shown_subtitle_ = message;
        player_scrolling_text_set(subtitle_label_, message);
        lv_obj_set_style_text_color(
            subtitle_label_, ui_theme::color(failed ? ui_theme::colors().warning
                                                    : player_subtitle_color()), 0);
    }
    applyLockArtwork();
    updateLockText();
}

std::size_t AudioPlayerScreen::rowCount() const {
    return playlist_->size();
}

int32_t AudioPlayerScreen::rowHeight() const {
    return kPlaylistRowHeight;
}

lv_obj_t *AudioPlayerScreen::createRow(lv_obj_t *parent) {
    lv_obj_t *row = lv_list_row_create(parent);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_hor(row, kPad, 0);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_set_style_bg_color(row, ui_theme::color(ui_theme::colors().selected), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_CHECKED);

    lv_obj_t *frame = lv_container_create(row, ui_theme::color(player_artwork_color()));
    lv_obj_set_size(frame, kThumbSide, kThumbSide);
    lv_obj_set_style_radius(frame, kThumbRadius, 0);
    lv_obj_set_style_clip_corner(frame, true, 0);
    lv_obj_set_clickable(frame, false);
    lv_obj_set_scrollable(frame, false);
    lv_obj_t *icon = lv_label_create(frame);
    lv_label_set_text(icon, TABLER_MUSIC);
    lv_obj_set_style_text_font(icon, &icon_28, 0);
    lv_obj_set_style_text_color(icon, ui_theme::color(player_artwork_icon_color()), 0);
    lv_obj_center(icon);

    lv_obj_t *text = lv_container_create(row);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_clickable(text, false);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *label = lv_label_create(text);
        lv_obj_set_size(label, lv_pct(100), body_line_height());
        lv_obj_set_font_role(label, LV_WIDGETS_FONT_BODY);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        if (i) lv_obj_set_style_text_color(label, ui_theme::color(player_subtitle_color()), 0);
    }
    return row;
}

void AudioPlayerScreen::bindRow(lv_obj_t *row, std::size_t index) {
    const PlaylistItem &item = playlist_->at(playlist_->orderAt(index));
    const std::shared_ptr<const MediaEntry> meta = media_cache_lookup(item.path);
    const std::string names = meta ? format_names(meta->artist, meta->album) : std::string();
    lv_obj_t *text = lv_obj_get_child(row, 1);
    lv_obj_t *subtitle = lv_obj_get_child(text, 1);
    lv_label_set_text(lv_obj_get_child(text, 0),
                      meta && meta->title[0] ? meta->title : item.name.c_str());
    lv_label_set_text(subtitle, names.c_str());
    lv_obj_set_hidden(subtitle, names.empty());
    const bool current = index == playlist_->position();
    lv_obj_set_state(row, LV_STATE_CHECKED, current);
    lv_obj_set_clickable(row, true);

    lv_obj_t *frame = lv_obj_get_child(row, 0);
    lv_obj_set_style_bg_color(
        frame, ui_theme::color(current ? ui_theme::colors().surface_pressed
                                       : player_artwork_color()), 0);
    std::shared_ptr<const ImagePixels> pixels = media_cache_thumbnail(item.path, kThumbBox);
    lv_obj_t *image = lv_obj_get_child_count(frame) > 1 ? lv_obj_get_child(frame, 1) : nullptr;
    if (image && lv_obj_get_user_data(image) != pixels.get()) {
        lv_obj_delete(image);
        image = nullptr;
    }
    if (!image && pixels) {
        const ImagePixels *shown = pixels.get();
        image = image_object_create(frame, std::move(pixels));
        if (image) {
            lv_obj_set_user_data(image, const_cast<ImagePixels *>(shown));
            lv_obj_set_size(image, kThumbSide, kThumbSide);
            lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER);
        }
    }
    lv_obj_set_hidden(lv_obj_get_child(frame, 0), image != nullptr);
}

void AudioPlayerScreen::didSelectRow(std::size_t index) {
    if (index == playlist_->position()) return;
    playlist_->jumpTo(index);
    auto_opened_ = false;
    skips_ = 0;
    openCurrent();
}

void AudioPlayerScreen::onEnter() {
    s_active = this;
    media_cache_idle_cancel();
    if (!token_) token_ = media_cache_token();
    if (!rows_token_) rows_token_ = media_cache_token();
    media_cache_observe(token_, metaReady);
    player_observe_state(playerStateChanged);
    openCurrent();
    timer_ = lv_timer_create([](lv_timer_t *timer) {
        static_cast<AudioPlayerScreen *>(lv_timer_get_user_data(timer))->handleState();
    }, kRefreshPeriodMs, this);
}

void AudioPlayerScreen::onExit() {
    if (lock_state_ != LockState::Unlocked) {
        lock_state_ = LockState::Unlocked;
        bsp_display_set_power(BSP_DISPLAY_POWER_ON);
        bsp_display_set_brightness(settings_display_brightness());
    }
    if (lv_group_t *group = lv_group_get_default()) lv_group_focus_freeze(group, false);
    if (s_active == this) s_active = nullptr;
    media_cache_unobserve(token_);
    media_cache_cancel(token_);
    media_cache_cancel(rows_token_);
    player_observe_state(nullptr);
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    player_close();
}

AudioPlayerScreen::~AudioPlayerScreen() {
    if (timer_) lv_timer_delete(timer_);
}

void AudioPlayerScreen::eject(const std::string &mount_point) {
    if (s_active && path_is_under(s_active->path(), mount_point)) s_active->back();
}
