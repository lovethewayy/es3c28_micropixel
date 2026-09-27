// Don't Tap the White Tile for MicroPixel: black tiles fall down four lanes,
// tap the falling black tile before it reaches the bottom - tap a white spot
// or miss the tile and the run is over. Speed ramps up as the score climbs.

#include "donttapwhite_app.hpp"

#include <stdint.h>

#include "sdk/micropixel.hpp"

namespace donttapwhite {

using Line = micropixel::FixedString<64U>;

namespace {

constexpr int32_t kTileColumns = 4;
constexpr int32_t kTileHeight = 96;
constexpr int32_t kFrameIntervalUs = 20000;  // 50 fps simulation/render.
constexpr float kStartSpeed = 260.0F;        // px/s.
constexpr float kSpeedStep = 18.0F;          // Extra px/s per 8 points.
constexpr float kMaxSpeed = 780.0F;

const auto kWhite = micropixel::Color::Rgb(250U, 250U, 248U);
const auto kBlack = micropixel::Color::Rgb(24U, 24U, 26U);
const auto kGray = micropixel::Color::Rgb(150U, 150U, 154U);
const auto kAccent = micropixel::Color::Rgb(236U, 68U, 58U);
const auto kDarkInk = micropixel::Color::Rgb(40U, 40U, 44U);

}  // namespace

int DontTapWhiteAppMain() {
    micropixel::Application app;
    app.renderer().ConfigureDisplay({}).value();
    const micropixel::RendererInfo display = app.renderer().info();
    const uint32_t buffers = (display.physical_width() > 480U || display.physical_height() > 480U) ? 3U : 2U;
    auto created = app.renderer().CreateHostSurface(buffers, 1U);
    if (!created.has_value()) {
        app.log().Error("donttapwhite: cannot allocate display buffers");
        return 1;
    }
    micropixel::HostSurface surface = static_cast<micropixel::HostSurface&&>(created.value());
    micropixel::Timer frame_timer =
        app.timers().Every(micropixel::Duration::Microseconds(static_cast<uint32_t>(kFrameIntervalUs))).value();

    const int32_t view_w = static_cast<int32_t>(surface.buffer_width());
    const int32_t view_h = static_cast<int32_t>(surface.buffer_height());
    const int32_t tile_w = view_w / kTileColumns;
    const int32_t hud_height = 132;

    uint32_t best = app.storage().GetU32Or("best", 0U);

    // --- Game state -------------------------------------------------------
    int32_t score = 0;
    bool running = false;      // kPlaying
    int32_t tile_col = 0;      // 0..3
    float tile_y = -static_cast<float>(kTileHeight);  // Top of the falling tile.
    float speed = kStartSpeed;
    uint64_t last_us = 0U;

    const auto spawn_tile = [&]() {
        tile_col = static_cast<int32_t>(app.random().Below(static_cast<uint32_t>(kTileColumns)));
        tile_y = -static_cast<float>(kTileHeight);
    };

    const auto reset_game = [&]() {
        score = 0;
        speed = kStartSpeed;
        running = true;
        spawn_tile();
    };

    reset_game();

    // --- Sound helpers ----------------------------------------------------
    const auto sfx = [&](uint32_t frequency, uint16_t duration_ms, uint16_t volume) {
        if (!app.audio().info().has_value() || !app.audio().info()->Supports(micropixel::Waveform::kSquare)) {
            return;
        }
        micropixel::Tone tone{};
        tone.waveform = micropixel::Waveform::kSquare;
        tone.frequency_hz = frequency;
        tone.duration = micropixel::Duration::Milliseconds(duration_ms);
        tone.volume_per_mille = volume;
        tone.attack = micropixel::Duration::Milliseconds(2U);
        tone.release = micropixel::Duration::Milliseconds(18U);
        (void)app.audio().Play(tone);
    };

    // --- Render ------------------------------------------------------------
    const auto render = [&]() {
        uint32_t index = 0U;
        if (!surface.AcquireFree(index)) {
            return;
        }
        bool ok = true;
        const auto updated = surface.Update(index, [&](micropixel::RasterDrawList& list) {
            // White field with a subtle lane grid.
            ok = list.FillRect({0, 0, view_w, view_h}, kWhite) && ok;
            for (int32_t col = 1; col < kTileColumns; ++col) {
                ok = list.FillRect({col * tile_w, 0, 1, view_h}, kGray) && ok;
            }

            // HUD.
            ok = list.FillRect({0, 0, view_w, hud_height - 40}, kWhite) && ok;
            Line score_line;
            score_line.AppendUint(static_cast<uint32_t>(score));
            const auto score_measured = app.renderer().MeasureText(score_line.c_str(), micropixel::SystemFont::kTitle);
            if (score_measured.has_value()) {
                const int32_t sx = (view_w - static_cast<int32_t>(score_measured->width)) / 2;
                ok = list.Text({sx, 18}, score_line.c_str(), kDarkInk, micropixel::SystemFont::kTitle) && ok;
            }
            Line best_line;
            best_line.Append("BEST ");
            best_line.AppendUint(best);
            ok = list.Text({view_w - 170, 96}, best_line.c_str(), kGray, micropixel::SystemFont::kSmall) && ok;

            // The falling tile; turns red as it nears the bottom.
            if (running) {
                const int32_t ty = static_cast<int32_t>(tile_y);
                const float danger = static_cast<float>(view_h - hud_height - ty) / static_cast<float>(view_h);
                const auto tile_color = danger < 0.22F ? kAccent : kBlack;
                ok = list.FillRect({tile_col * tile_w, ty, tile_w, kTileHeight}, tile_color) && ok;
            }

            // Game over overlay.
            if (!running) {
                ok = list.FillRect({0, view_h / 2 - 96, view_w, 192}, kDarkInk) && ok;
                ok = list.Text({view_w / 2 - 130, view_h / 2 - 64}, "GAME OVER", kWhite,
                               micropixel::SystemFont::kTitle) && ok;
                Line over_score;
                over_score.Append("SCORE ");
                over_score.AppendUint(static_cast<uint32_t>(score));
                const auto sub_measured = app.renderer().MeasureText(over_score.c_str(), micropixel::SystemFont::kMedium);
                if (sub_measured.has_value()) {
                    ok = list.Text({view_w / 2 - static_cast<int32_t>(sub_measured->width) / 2, view_h / 2 + 8},
                                   over_score.c_str(), kWhite, micropixel::SystemFont::kMedium) && ok;
                }
            }
        });
        if (!updated.has_value() || !ok || !surface.Present(index).has_value()) {
            app.log().Error("donttapwhite: frame submission failed");
        }
    };

    // --- Main loop ---------------------------------------------------------
    app.Run([&](const micropixel::Event& event) {
        const uint64_t now_us = event.timestamp().microseconds();
        if (last_us == 0U) {
            last_us = now_us;
        }
        int32_t dt_ms = static_cast<int32_t>((now_us - last_us) / 1000U);
        if (dt_ms < 0) {
            dt_ms = 0;
        }
        if (dt_ms > 40) {
            dt_ms = 40;
        }
        last_us = now_us;

        if (event.type() == micropixel::EventType::kStop) {
            if (static_cast<uint32_t>(score) > best) {
                best = static_cast<uint32_t>(score);
                (void)app.storage().SetU32("best", best);
            }
            return micropixel::EventResult::kExit;
        }
        if (event.type() == micropixel::EventType::kResume) {
            render();
        }

        if (running) {
            const float dt_s = static_cast<float>(dt_ms) * 0.001F;
            tile_y += speed * dt_s;
            if (tile_y > static_cast<float>(view_h - hud_height)) {
                // Missed the tile.
                running = false;
                if (static_cast<uint32_t>(score) > best) {
                    best = static_cast<uint32_t>(score);
                    (void)app.storage().SetU32("best", best);
                }
                sfx(150U, 200U, 90U);
            }
        }

        if (const micropixel::TouchEvent* touch = event.touch()) {
            if (touch->phase() == micropixel::TouchPhase::kDown) {
                const int32_t tx = touch->x();
                const int32_t ty = touch->y();
                if (running) {
                    const int32_t tile_x0 = tile_col * tile_w;
                    const int32_t tile_y0 = static_cast<int32_t>(tile_y);
                    const bool on_tile = tx >= tile_x0 && tx < tile_x0 + tile_w && ty >= tile_y0 &&
                                         ty < tile_y0 + kTileHeight;
                    if (on_tile) {
                        ++score;
                        speed = kStartSpeed + kSpeedStep * static_cast<float>(score / 8);
                        if (speed > kMaxSpeed) {
                            speed = kMaxSpeed;
                        }
                        sfx(760U + static_cast<uint16_t>((score % 12) * 24U), 50U, 90U);
                        spawn_tile();
                    } else {
                        running = false;
                        if (static_cast<uint32_t>(score) > best) {
                            best = static_cast<uint32_t>(score);
                            (void)app.storage().SetU32("best", best);
                        }
                        sfx(150U, 200U, 90U);
                    }
                } else if (ty > view_h / 2 - 96 && ty < view_h / 2 + 96) {
                    reset_game();
                    (void)tx;
                }
            }
        }

        if (event.TimerFrom(frame_timer) != nullptr || event.type() == micropixel::EventType::kResume) {
            render();
        }
        return micropixel::EventResult::kContinue;
    });

    if (static_cast<uint32_t>(score) > best) {
        best = static_cast<uint32_t>(score);
        (void)app.storage().SetU32("best", best);
    }
    return 0;
}

}  // namespace donttapwhite
