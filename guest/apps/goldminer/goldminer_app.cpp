// Gold Miner for MicroPixel: swing the claw, tap to drop it, drag treasure
// back up before the clock runs out. A touch game rendered through the
// HostSurface raster list (pure fills + flat polygons, no textures).

#include "goldminer_app.hpp"

#include <stdint.h>

#include <span>

#include "sdk/math.hpp"
#include "sdk/micropixel.hpp"

namespace goldminer {

using Line = micropixel::FixedString<96U>;
namespace math = micropixel::math;

namespace {

constexpr int32_t kCanvasWidth = 720;
constexpr int32_t kCanvasHeight = 720;
constexpr int32_t kGroundY = 110;      // Top of the underground.
constexpr int32_t kMachineX = 360;     // Winch anchor on the surface.
constexpr int32_t kMachineY = 64;      // Winch vertical position.
constexpr int32_t kBottomLimit = 692;  // Floor of the dig site.
constexpr int32_t kMaxItems = 15;
constexpr int32_t kMaxSpawnAttempts = 120;
constexpr float kMaxSwing = 1.05F;     // +/- 60 degrees from vertical.
constexpr float kSwingRate = 0.85F;    // Radians per second of the pendulum phase.
constexpr float kSwingRadius = 58.0F;
constexpr uint32_t kLevelDurationMs = 60000U;
constexpr int32_t kFrameIntervalUs = 33333;  // ~30 fps animation timer.

// Palette slots for flat polygons (index into the 256-entry flat palette).
constexpr uint8_t kPalGold = 1U;
constexpr uint8_t kPalGoldBig = 2U;
constexpr uint8_t kPalGoldHi = 3U;
constexpr uint8_t kPalDiamond = 4U;
constexpr uint8_t kPalDiamondHi = 5U;
constexpr uint8_t kPalRock = 6U;
constexpr uint8_t kPalRockDark = 7U;
constexpr uint8_t kPalRope = 8U;
constexpr uint8_t kPalClaw = 9U;
constexpr uint8_t kPalClawDark = 10U;

enum class ItemKind : uint8_t { kSmallGold, kBigGold, kDiamond, kSmallRock, kBigRock };

struct Item final {
    ItemKind kind{};
    int32_t x{};
    int32_t y{};
    int32_t radius{};
    int32_t value{};
    bool alive{};
    bool grabbed{};
};

enum class ClawState : uint8_t { kSwing, kExtend, kRetract };
enum class GamePhase : uint8_t { kPlaying, kLevelClear, kGameOver };

[[nodiscard]] constexpr int32_t ItemValue(ItemKind kind) {
    switch (kind) {
        case ItemKind::kSmallGold:
            return 250;
        case ItemKind::kBigGold:
            return 1000;
        case ItemKind::kDiamond:
            return 600;
        default:
            return 0;
    }
}

[[nodiscard]] constexpr int32_t ItemRadius(ItemKind kind) {
    switch (kind) {
        case ItemKind::kSmallGold:
            return 18;
        case ItemKind::kBigGold:
            return 30;
        case ItemKind::kDiamond:
            return 22;
        case ItemKind::kSmallRock:
            return 22;
        default:
            return 36;
    }
}

[[nodiscard]] constexpr uint8_t ItemPalette(ItemKind kind) {
    switch (kind) {
        case ItemKind::kSmallGold:
            return kPalGold;
        case ItemKind::kBigGold:
            return kPalGoldBig;
        case ItemKind::kDiamond:
            return kPalDiamond;
        case ItemKind::kSmallRock:
            return kPalRock;
        default:
            return kPalRockDark;
    }
}

[[nodiscard]] constexpr bool IsRock(ItemKind kind) {
    return kind == ItemKind::kSmallRock || kind == ItemKind::kBigRock;
}

[[nodiscard]] uint16_t Rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return micropixel::Color::Rgb(r, g, b).rgb565();
}

}  // namespace

int GoldMinerAppMain() {
    micropixel::Application app;
    app.renderer().ConfigureDisplay({}).value();  // Native pixels: touch arrives in buffer coordinates.
    const micropixel::RendererInfo display = app.renderer().info();
    if (!display.polygon_supported()) {
        app.log().Error("goldminer: polygon raster support is required");
        return 1;
    }
    const uint32_t buffers = (display.physical_width() > 480U || display.physical_height() > 480U) ? 3U : 2U;
    auto created = app.renderer().CreateHostSurface(buffers, 1U);
    if (!created.has_value()) {
        app.log().Error("goldminer: cannot allocate display buffers");
        return 1;
    }
    micropixel::HostSurface surface = static_cast<micropixel::HostSurface&&>(created.value());
    micropixel::RasterResources raster = app.renderer().CreateRasterResources().value();
    micropixel::Timer frame_timer =
        app.timers().Every(micropixel::Duration::Microseconds(static_cast<uint32_t>(kFrameIntervalUs))).value();

    // Flat palette: slot 0 is opaque black, slots 1..10 hold the game colors,
    // everything else is a neutral gray. One light level keeps flat polygons
    // exactly the color we ask for.
    uint16_t palette[256]{};
    palette[0] = Rgb565(0U, 0U, 0U);
    palette[kPalGold] = Rgb565(255U, 204U, 40U);
    palette[kPalGoldBig] = Rgb565(255U, 176U, 24U);
    palette[kPalGoldHi] = Rgb565(255U, 236U, 150U);
    palette[kPalDiamond] = Rgb565(90U, 210U, 255U);
    palette[kPalDiamondHi] = Rgb565(210U, 244U, 255U);
    palette[kPalRock] = Rgb565(158U, 158U, 168U);
    palette[kPalRockDark] = Rgb565(112U, 112U, 122U);
    palette[kPalRope] = Rgb565(150U, 102U, 52U);
    palette[kPalClaw] = Rgb565(206U, 206U, 212U);
    palette[kPalClawDark] = Rgb565(128U, 128U, 138U);
    for (uint32_t i = 11U; i < 256U; ++i) {
        palette[i] = Rgb565(110U, 110U, 120U);
    }
    (void)raster.UploadLitPalette(0U, 1U, std::span<const uint16_t>(palette, 256U));

    // --- Game state -------------------------------------------------------
    const int32_t view_w = static_cast<int32_t>(surface.buffer_width());
    const int32_t view_h = static_cast<int32_t>(surface.buffer_height());

    Item items[kMaxItems]{};
    int32_t item_count = 0;
    uint32_t money = 0U;
    uint32_t best = app.storage().GetU32Or("best", 0U);
    int32_t level = 1;
    uint32_t target = 500U;
    int32_t time_left_ms = static_cast<int32_t>(kLevelDurationMs);
    GamePhase phase = GamePhase::kPlaying;
    ClawState claw_state = ClawState::kSwing;
    float swing_phase = 0.0F;
    float claw_angle = 0.0F;
    float claw_x = static_cast<float>(kMachineX);
    float claw_y = static_cast<float>(kGroundY) + kSwingRadius;
    float claw_dx = 0.0F;
    float claw_dy = 1.0F;
    Item* grabbed_item = nullptr;
    int32_t grab_ox = 0;
    int32_t grab_oy = 0;
    int32_t rock_struggle_ms = 0;

    micropixel::Random random = app.random();

    const auto spawn_items = [&]() {
        for (int32_t i = 0; i < kMaxItems; ++i) {
            items[i].alive = false;
            items[i].grabbed = false;
        }
        item_count = 0;
        for (int32_t i = 0; i < kMaxItems; ++i) {
            const uint32_t roll = random.Below(100U);
            ItemKind kind = ItemKind::kSmallGold;
            if (roll < 35U) {
                kind = ItemKind::kSmallGold;
            } else if (roll < 55U) {
                kind = ItemKind::kDiamond;
            } else if (roll < 72U) {
                kind = ItemKind::kBigGold;
            } else if (roll < 88U) {
                kind = ItemKind::kSmallRock;
            } else {
                kind = ItemKind::kBigRock;
            }
            const int32_t radius = ItemRadius(kind);
            int32_t x = 0;
            int32_t y = 0;
            bool placed = false;
            for (int32_t attempt = 0; attempt < kMaxSpawnAttempts; ++attempt) {
                x = 50 + static_cast<int32_t>(random.Below(static_cast<uint32_t>(kCanvasWidth - 100)));
                y = kGroundY + 44 +
                    static_cast<int32_t>(random.Below(static_cast<uint32_t>(kBottomLimit - kGroundY - 84)));
                bool overlap = false;
                for (int32_t j = 0; j < item_count; ++j) {
                    if (!items[j].alive) {
                        continue;
                    }
                    const int32_t dx = items[j].x - x;
                    const int32_t dy = items[j].y - y;
                    const int32_t min_dist = items[j].radius + radius + 14;
                    if (dx * dx + dy * dy < min_dist * min_dist) {
                        overlap = true;
                        break;
                    }
                }
                if (!overlap) {
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                continue;
            }
            items[item_count].kind = kind;
            items[item_count].x = x;
            items[item_count].y = y;
            items[item_count].radius = radius;
            items[item_count].value = ItemValue(kind);
            items[item_count].alive = true;
            items[item_count].grabbed = false;
            ++item_count;
        }
    };

    const auto start_level = [&](int32_t next_level) {
        level = next_level;
        uint32_t t = 500U;
        for (int32_t i = 1; i < level; ++i) {
            t = t * 16U / 10U;
        }
        target = ((t + 9U) / 10U) * 10U;
        time_left_ms = static_cast<int32_t>(kLevelDurationMs);
        phase = GamePhase::kPlaying;
        claw_state = ClawState::kSwing;
        swing_phase = 0.0F;
        claw_angle = 0.0F;
        claw_x = static_cast<float>(kMachineX);
        claw_y = static_cast<float>(kGroundY) + kSwingRadius;
        claw_dx = 0.0F;
        claw_dy = 1.0F;
        grabbed_item = nullptr;
        rock_struggle_ms = 0;
        spawn_items();
    };

    start_level(1);

    // --- Sound helpers ----------------------------------------------------
    const auto sfx = [&](uint32_t frequency, uint16_t duration_ms, uint16_t volume) {
        if (!app.audio().info().has_value() || !app.audio().info()->Supports(micropixel::Waveform::kTriangle)) {
            return;
        }
        micropixel::Tone tone{};
        tone.waveform = micropixel::Waveform::kTriangle;
        tone.frequency_hz = frequency;
        tone.duration = micropixel::Duration::Milliseconds(duration_ms);
        tone.volume_per_mille = volume;
        tone.attack = micropixel::Duration::Milliseconds(4U);
        tone.release = micropixel::Duration::Milliseconds(30U);
        (void)app.audio().Play(tone);
    };

    // --- Drawing helpers ---------------------------------------------------
    const auto draw_rope = [&](micropixel::RasterDrawList& list) {
        const float x0 = static_cast<float>(kMachineX);
        const float y0 = static_cast<float>(kMachineY);
        const float dx = claw_x - x0;
        const float dy = claw_y - y0;
        const float len = math::Sqrt(dx * dx + dy * dy);
        const float inv = len > 0.5F ? 1.0F / len : 1.0F;
        const float nx = -dy * inv;
        const float ny = dx * inv;
        const float half = 2.0F;
        micropixel::RasterVertex corners[4] = {
            micropixel::RasterVertex::At(x0 + nx * half, y0 + ny * half, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(claw_x + nx * half, claw_y + ny * half, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(claw_x - nx * half, claw_y - ny * half, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(x0 - nx * half, y0 - ny * half, 0.0F, 0.0F, 0U),
        };
        (void)list.FlatQuad(corners, kPalRope);
    };

    const auto draw_claw = [&](micropixel::RasterDrawList& list) {
        const float dx = claw_dx;
        const float dy = claw_dy;
        const float px = -dy;
        const float py = dx;
        // Claw body: a short bar just above the tip.
        micropixel::RasterVertex body_corners[4] = {
            micropixel::RasterVertex::At(claw_x + px * 8.0F - dx * 16.0F, claw_y + py * 8.0F - dy * 16.0F, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(claw_x + px * 8.0F, claw_y + py * 8.0F, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(claw_x - px * 8.0F, claw_y - py * 8.0F, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(claw_x - px * 8.0F - dx * 16.0F, claw_y - py * 8.0F - dy * 16.0F, 0.0F, 0.0F, 0U),
        };
        (void)list.FlatQuad(body_corners, kPalClaw);
        // Two jaws opening in the direction of travel.
        const float spread = 13.0F;
        micropixel::RasterVertex jaw_left[3] = {
            micropixel::RasterVertex::At(claw_x, claw_y, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(claw_x + px * spread + dx * 10.0F, claw_y + py * spread + dy * 10.0F, 0.0F,
                                         0.0F, 0U),
            micropixel::RasterVertex::At(claw_x + px * spread + dx * 22.0F, claw_y + py * spread + dy * 22.0F, 0.0F,
                                         0.0F, 0U),
        };
        micropixel::RasterVertex jaw_right[3] = {
            micropixel::RasterVertex::At(claw_x, claw_y, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(claw_x - px * spread + dx * 10.0F, claw_y - py * spread + dy * 10.0F, 0.0F,
                                         0.0F, 0U),
            micropixel::RasterVertex::At(claw_x - px * spread + dx * 22.0F, claw_y - py * spread + dy * 22.0F, 0.0F,
                                         0.0F, 0U),
        };
        (void)list.FlatTriangle(jaw_left, kPalClawDark);
        (void)list.FlatTriangle(jaw_right, kPalClawDark);
    };

    const auto draw_item = [&](micropixel::RasterDrawList& list, const Item& item) {
        const float cx = static_cast<float>(item.x);
        const float cy = static_cast<float>(item.y);
        const float r = static_cast<float>(item.radius);
        if (IsRock(item.kind)) {
            // Square rock with a darker inset facet.
            micropixel::RasterVertex outer[4] = {
                micropixel::RasterVertex::At(cx - r, cy - r, 0.0F, 0.0F, 0U),
                micropixel::RasterVertex::At(cx + r, cy - r, 0.0F, 0.0F, 0U),
                micropixel::RasterVertex::At(cx + r, cy + r, 0.0F, 0.0F, 0U),
                micropixel::RasterVertex::At(cx - r, cy + r, 0.0F, 0.0F, 0U),
            };
            (void)list.FlatQuad(outer, ItemPalette(item.kind));
            const float inset = r * 0.45F;
            micropixel::RasterVertex inner[4] = {
                micropixel::RasterVertex::At(cx - inset, cy - inset, 0.0F, 0.0F, 0U),
                micropixel::RasterVertex::At(cx + inset, cy - inset, 0.0F, 0.0F, 0U),
                micropixel::RasterVertex::At(cx + inset, cy + inset, 0.0F, 0.0F, 0U),
                micropixel::RasterVertex::At(cx - inset, cy + inset, 0.0F, 0.0F, 0U),
            };
            (void)list.FlatQuad(inner, item.kind == ItemKind::kSmallRock ? kPalRockDark : kPalRock);
            return;
        }
        // Gem: diamond shape with a bright top-left highlight.
        const float half_w = r * 0.85F;
        micropixel::RasterVertex gem[4] = {
            micropixel::RasterVertex::At(cx, cy - r, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(cx + half_w, cy, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(cx, cy + r, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(cx - half_w, cy, 0.0F, 0.0F, 0U),
        };
        (void)list.FlatQuad(gem, ItemPalette(item.kind));
        const float hi = r * 0.32F;
        micropixel::RasterVertex hi_gem[3] = {
            micropixel::RasterVertex::At(cx, cy - r * 0.92F, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(cx + hi, cy - r * 0.28F, 0.0F, 0.0F, 0U),
            micropixel::RasterVertex::At(cx - hi * 0.5F, cy - r * 0.28F, 0.0F, 0.0F, 0U),
        };
        (void)list.FlatTriangle(hi_gem, item.kind == ItemKind::kDiamond ? kPalDiamondHi : kPalGoldHi);
    };

    const auto draw_hud = [&](micropixel::RasterDrawList& list) {
        const auto dark = micropixel::Color::Rgb(52U, 42U, 30U);
        (void)list.Text({8, 10}, "TIME", dark, micropixel::SystemFont::kSmall);
        Line time_line;
        time_line.AppendUint(static_cast<uint32_t>(time_left_ms > 0 ? time_left_ms / 1000 : 0));
        time_line.Append("s");
        (void)list.Text({8, 32}, time_line.c_str(), dark, micropixel::SystemFont::kMedium);
        const int32_t bar_w = view_w - 250;
        const float frac = static_cast<float>(time_left_ms) / static_cast<float>(kLevelDurationMs);
        const float clamped = frac < 0.0F ? 0.0F : (frac > 1.0F ? 1.0F : frac);
        const int32_t filled = static_cast<int32_t>(static_cast<float>(bar_w) * clamped);
        (void)list.FillRect({8, 64, bar_w, 12}, micropixel::Color::Rgb(214U, 204U, 190U));
        const auto bar_color = frac > 0.35F ? micropixel::Color::Rgb(96U, 190U, 90U) : micropixel::Color::Rgb(232U, 92U, 74U);
        if (filled > 0) {
            (void)list.FillRect({8, 64, filled, 12}, bar_color);
        }

        Line goal_line;
        goal_line.Append("GOAL ");
        goal_line.AppendUint(target);
        (void)list.Text({view_w - 212, 10}, goal_line.c_str(), dark, micropixel::SystemFont::kSmall);
        Line money_line;
        money_line.Append("$");
        money_line.AppendUint(money);
        (void)list.Text({view_w - 152, 26}, money_line.c_str(), dark, micropixel::SystemFont::kTitle);
        Line best_line;
        best_line.Append("BEST ");
        best_line.AppendUint(best);
        (void)list.Text({view_w - 152, 66}, best_line.c_str(), dark, micropixel::SystemFont::kSmall);
    };

    const auto draw_overlay = [&](micropixel::RasterDrawList& list) {
        if (phase == GamePhase::kPlaying) {
            return;
        }
        const int32_t cx = view_w / 2;
        const int32_t cy = view_h / 2;
        (void)list.FillRect({0, cy - 92, view_w, 150}, micropixel::Color::Rgb(24U, 20U, 16U), 210);
        const auto white = micropixel::Color::Rgb(255U, 255U, 250U);
        if (phase == GamePhase::kLevelClear) {
            (void)list.Text({cx - 130, cy - 60}, "LEVEL CLEAR!", white, micropixel::SystemFont::kTitle);
            Line sub;
            sub.Append("TAP TO DIG LEVEL ");
            sub.AppendUint(static_cast<uint32_t>(level + 1));
            const auto measured = app.renderer().MeasureText(sub.c_str(), micropixel::SystemFont::kMedium);
            if (measured.has_value()) {
                (void)list.Text({cx - static_cast<int32_t>(measured->width) / 2, cy + 16}, sub.c_str(), white,
                                micropixel::SystemFont::kMedium);
            }
        } else {
            (void)list.Text({cx - 120, cy - 60}, "TIME'S UP", micropixel::Color::Rgb(240U, 100U, 80U),
                            micropixel::SystemFont::kTitle);
            const auto measured = app.renderer().MeasureText("TAP TO RESTART", micropixel::SystemFont::kMedium);
            if (measured.has_value()) {
                (void)list.Text({cx - static_cast<int32_t>(measured->width) / 2, cy + 16}, "TAP TO RESTART", white,
                                micropixel::SystemFont::kMedium);
            }
        }
    };

    const auto render = [&]() {
        uint32_t index = 0U;
        if (!surface.AcquireFree(index)) {
            return;
        }
        bool ok = true;
        const auto updated = surface.Update(index, [&](micropixel::RasterDrawList& list) {
            ok = list.FillRect({0, 0, view_w, kGroundY}, micropixel::Color::Rgb(150U, 208U, 244U)) && ok;
            ok = list.FillRect({0, kGroundY, view_w, 12}, micropixel::Color::Rgb(96U, 196U, 84U)) && ok;
            ok = list.FillRect({0, kGroundY + 12, view_w, view_h - kGroundY - 12},
                               micropixel::Color::Rgb(152U, 112U, 70U)) && ok;
            for (int32_t y = kGroundY + 96; y < view_h; y += 150) {
                ok = list.FillRect({0, y, view_w, 8}, micropixel::Color::Rgb(136U, 98U, 60U)) && ok;
            }
            for (int32_t i = 0; i < item_count; ++i) {
                if (items[i].alive) {
                    draw_item(list, items[i]);
                }
            }
            draw_rope(list);
            draw_claw(list);
            draw_hud(list);
            draw_overlay(list);
        });
        if (!updated.has_value() || !ok || !surface.Present(index).has_value()) {
            app.log().Error("goldminer: frame submission failed");
        }
    };

    // --- Simulation --------------------------------------------------------
    const auto finish_level_if_timeout = [&]() {
        if (time_left_ms > 0) {
            return;
        }
        if (money >= target) {
            phase = GamePhase::kLevelClear;
            sfx(660U, 90U, 90U);
        } else {
            phase = GamePhase::kGameOver;
            sfx(220U, 160U, 90U);
        }
    };

    uint64_t last_us = 0U;

    app.Run([&](const micropixel::Event& event) {
        const uint64_t now_us = event.timestamp().microseconds();
        if (last_us == 0U) {
            last_us = now_us;
        }
        int32_t dt_ms = static_cast<int32_t>((now_us - last_us) / 1000U);
        if (dt_ms < 0) {
            dt_ms = 0;
        }
        if (dt_ms > 66) {
            dt_ms = 66;
        }
        last_us = now_us;

        if (event.type() == micropixel::EventType::kStop) {
            if (money > best) {
                best = money;
                (void)app.storage().SetU32("best", best);
            }
            return micropixel::EventResult::kExit;
        }
        if (event.type() == micropixel::EventType::kResume) {
            render();
        }

        const float dt_s = static_cast<float>(dt_ms) * 0.001F;

        if (phase == GamePhase::kPlaying) {
            time_left_ms -= dt_ms;
            if (time_left_ms < 0) {
                time_left_ms = 0;
            }

            if (claw_state == ClawState::kSwing) {
                swing_phase += dt_s * kSwingRate;
                claw_angle = kMaxSwing * math::Sin(swing_phase);
                const float sx = math::Sin(claw_angle);
                const float cy = math::Cos(claw_angle);
                claw_x = static_cast<float>(kMachineX) + sx * kSwingRadius;
                claw_y = static_cast<float>(kGroundY) + cy * kSwingRadius;
                claw_dx = sx;
                claw_dy = cy;
                finish_level_if_timeout();
            } else if (claw_state == ClawState::kExtend) {
                const float speed = 150.0F;
                claw_x += claw_dx * speed * dt_s;
                claw_y += claw_dy * speed * dt_s;
                if (grabbed_item == nullptr) {
                    for (int32_t i = 0; i < item_count; ++i) {
                        if (!items[i].alive || items[i].grabbed) {
                            continue;
                        }
                        const float dx = static_cast<float>(items[i].x) - claw_x;
                        const float dy = static_cast<float>(items[i].y) - claw_y;
                        const float radius = static_cast<float>(items[i].radius) + 6.0F;
                        if (dx * dx + dy * dy <= radius * radius) {
                            grabbed_item = &items[i];
                            items[i].grabbed = true;
                            grab_ox = items[i].x - static_cast<int32_t>(claw_x);
                            grab_oy = items[i].y - static_cast<int32_t>(claw_y);
                            rock_struggle_ms = 0;
                            if (items[i].value > 0) {
                                sfx(720U, 60U, 80U);
                            }
                            break;
                        }
                    }
                }
                if (grabbed_item != nullptr) {
                    grabbed_item->x = static_cast<int32_t>(claw_x) + grab_ox;
                    grabbed_item->y = static_cast<int32_t>(claw_y) + grab_oy;
                }
                if (claw_y > static_cast<float>(kBottomLimit) || claw_x < 12.0F ||
                    claw_x > static_cast<float>(view_w - 12.0F)) {
                    claw_state = ClawState::kRetract;
                }
            } else if (claw_state == ClawState::kRetract) {
                float speed = 300.0F;
                bool struggle = false;
                if (grabbed_item != nullptr) {
                    switch (grabbed_item->kind) {
                        case ItemKind::kSmallGold:
                            speed = 190.0F;
                            break;
                        case ItemKind::kBigGold:
                            speed = 120.0F;
                            break;
                        case ItemKind::kDiamond:
                            speed = 170.0F;
                            break;
                        case ItemKind::kSmallRock:
                            speed = 62.0F;
                            struggle = true;
                            break;
                        default:
                            speed = 36.0F;
                            struggle = true;
                            break;
                    }
                    if (struggle) {
                        rock_struggle_ms += dt_ms;
                        if (rock_struggle_ms >= 900) {
                            // Too heavy: the rock slips off and stays buried.
                            grabbed_item->alive = true;
                            grabbed_item->grabbed = false;
                            grabbed_item = nullptr;
                            sfx(150U, 140U, 80U);
                            rock_struggle_ms = 0;
                        }
                    } else {
                        grabbed_item->x = static_cast<int32_t>(claw_x) + grab_ox;
                        grabbed_item->y = static_cast<int32_t>(claw_y) + grab_oy;
                    }
                }
                claw_x -= claw_dx * speed * dt_s;
                claw_y -= claw_dy * speed * dt_s;
                const float home_x = static_cast<float>(kMachineX) + claw_dx * kSwingRadius;
                const float home_y = static_cast<float>(kGroundY) + claw_dy * kSwingRadius;
                const float dx = claw_x - home_x;
                const float dy = claw_y - home_y;
                if (dx * dx + dy * dy < 36.0F) {
                    if (grabbed_item != nullptr) {
                        money += static_cast<uint32_t>(grabbed_item->value);
                        if (money > best) {
                            best = money;
                            (void)app.storage().SetU32("best", best);
                        }
                        if (grabbed_item->value > 0) {
                            sfx(880U, 70U, 90U);
                        }
                        grabbed_item->alive = false;
                        grabbed_item->grabbed = false;
                        grabbed_item = nullptr;
                    }
                    claw_state = ClawState::kSwing;
                    finish_level_if_timeout();
                }
            }
        }

        if (const micropixel::TouchEvent* touch = event.touch()) {
            if (touch->phase() == micropixel::TouchPhase::kDown) {
                if (phase == GamePhase::kPlaying && claw_state == ClawState::kSwing) {
                    claw_state = ClawState::kExtend;
                } else if (phase == GamePhase::kLevelClear || phase == GamePhase::kGameOver) {
                    if (phase == GamePhase::kLevelClear) {
                        start_level(level + 1);
                    } else {
                        money = 0U;
                        start_level(1);
                    }
                }
            }
        }

        if (event.TimerFrom(frame_timer) != nullptr || event.type() == micropixel::EventType::kResume) {
            render();
        }
        return micropixel::EventResult::kContinue;
    });

    if (money > best) {
        best = money;
        (void)app.storage().SetU32("best", best);
    }
    return 0;
}

}  // namespace goldminer
