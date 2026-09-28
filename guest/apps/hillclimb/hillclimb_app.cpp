// Hill Climb: a small "hold to drive" physics racer for the MicroPixel
// ES3C28P display. Procedural terrain scrolls past a jeep that climbs hills,
// burns fuel and picks up jerry cans; crash or run dry and the run ends.
//
// Rendering (HostSurface + RasterDrawList): one full clear, a two-layer
// parallax mountain backdrop, ~90 terrain columns, a handful of quads for the
// car and wheels, plus HUD text. No textures are needed: the only palette
// entries used are the FlatQuad colors.
#include <stdint.h>

#include "hillclimb_app.hpp"
#include "sdk/micropixel.hpp"

namespace hillclimb {

namespace mp = micropixel;
using Line = mp::FixedString<96U>;

namespace {

// --- World / physical units (pixels, seconds) -----------------------------
constexpr float kPixelsPerMeter = 40.0F;
constexpr float kPhysicsDt = 1.0F / 120.0F;
constexpr uint32_t kMaxStepsPerFrame = 8U;

// Terrain.
constexpr float kGridInterval = 72.0F;
constexpr float kBaseY = 360.0F;
constexpr float kNoiseAmp = 18.0F;
constexpr float kHillAmp = 42.0F;
constexpr float kHillPeriod = 320.0F;
constexpr float kTrendAmp = 90.0F;
constexpr float kTrendPeriod = 1800.0F;

// Car geometry (local frame: +x is the nose).
constexpr float kHalfWheelBase = 34.0F;
constexpr float kWheelRadius = 22.0F;
constexpr float kBodyHalfWidth = 46.0F;
constexpr float kBodyHalfHeight = 14.0F;
constexpr float kCabinNose = 16.0F;
constexpr float kCabinTop = 10.0F;

// Physics tuning.
constexpr float kGravity = 340.0F;
constexpr float kEngineAccel = 420.0F;
constexpr float kDrag = 0.06F;
constexpr float kRollFriction = 0.55F;
constexpr float kGroundLerp = 0.55F;
constexpr float kCrashSpeed = 950.0F;
constexpr float kFlipLimit = 1.05F;

// Fuel.
constexpr float kFuelMax = 100.0F;
constexpr float kFuelBurn = 3.6F;
constexpr float kFuelGain = 55.0F;
constexpr float kCanSpacingBase = 800.0F;
constexpr float kCanSpacingRange = 560.0F;

// Frame pacing.
constexpr uint32_t kFrameIntervalUs = 8000U;

// --- Palette (single light level) ------------------------------------------
constexpr uint8_t kPaletteSlot = 0U;
constexpr uint8_t kIdxWheel = 1U;
constexpr uint8_t kIdxBodyDark = 2U;
constexpr uint8_t kIdxBody = 3U;
constexpr uint8_t kIdxCabin = 4U;
constexpr uint8_t kIdxCan = 5U;
constexpr uint8_t kIdxCanDark = 6U;

// --- Colors ----------------------------------------------------------------
constexpr mp::Color kSky = mp::Color::Rgb(118U, 188U, 232U);
constexpr mp::Color kSkyDeep = mp::Color::Rgb(96U, 166U, 220U);
constexpr mp::Color kSkyPale = mp::Color::Rgb(152U, 204U, 236U);
constexpr mp::Color kMountainFar = mp::Color::Rgb(152U, 164U, 200U);
constexpr mp::Color kMountainNear = mp::Color::Rgb(128U, 148U, 186U);
constexpr mp::Color kGround = mp::Color::Rgb(96U, 168U, 84U);
constexpr mp::Color kGroundTop = mp::Color::Rgb(142U, 206U, 108U);
constexpr mp::Color kHudText = mp::Color::Rgb(250U, 250U, 250U);
constexpr mp::Color kHudDim = mp::Color::Rgb(210U, 230U, 240U);
constexpr mp::Color kHudShadow = mp::Color::Rgb(20U, 34U, 44U);
constexpr mp::Color kFuelBg = mp::Color::Rgb(30U, 42U, 52U);
constexpr mp::Color kFuelGood = mp::Color::Rgb(246U, 200U, 40U);
constexpr mp::Color kFuelLow = mp::Color::Rgb(232U, 64U, 44U);
constexpr mp::Color kOverlay = mp::Color::Rgb(18U, 28U, 38U);

enum class OverReason : uint8_t { kNone, kCrash, kOutOfFuel };

struct Palette {
    uint16_t rgb565[256]{};
};

[[nodiscard]] uint16_t Rgb565(float r, float g, float b) {
    const auto channel = [](float value) {
        value = mp::math::Clamp(value, 0.0F, 255.0F);
        return static_cast<uint16_t>(static_cast<uint32_t>(value * 31.0F / 255.0F + 0.5F));
    };
    const auto channel6 = [](float value) {
        value = mp::math::Clamp(value, 0.0F, 255.0F);
        return static_cast<uint16_t>(static_cast<uint32_t>(value * 63.0F / 255.0F + 0.5F));
    };
    return static_cast<uint16_t>((channel(r) << 11) | (channel6(g) << 5) | channel(b));
}

// --- Deterministic hash noise (no libc, infinite x) -------------------------
[[nodiscard]] uint32_t HashMix(uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

[[nodiscard]] float HashUnit(int32_t grid, uint32_t seed) {
    const uint32_t mixed = HashMix(static_cast<uint32_t>(grid) * 0x9E3779B1U ^ seed * 0x85EBCA6BU ^ 0x1234567FU);
    return static_cast<float>(mixed) * (1.0F / 4294967296.0F);
}

// Smooth interpolated value noise in [-1, 1].
[[nodiscard]] float Noise1D(float x, uint32_t seed) {
    const float g = x / kGridInterval;
    const int32_t i0 = mp::math::FloorToInt(g);
    const float t = mp::math::Clamp(g - static_cast<float>(i0), 0.0F, 1.0F);
    const float w = mp::math::SmoothStep(t);
    const float a = HashUnit(i0, seed) * 2.0F - 1.0F;
    const float b = HashUnit(i0 + 1, seed) * 2.0F - 1.0F;
    return mp::math::Lerp(a, b, w);
}

// Terrain surface y (screen/world coords, y grows downward) at world x.
[[nodiscard]] float TerrainAt(float x, uint32_t seed) {
    const float noise = Noise1D(x, seed) * kNoiseAmp;
    const float hill = mp::math::Sin(x / kHillPeriod * mp::math::kTwoPi + HashUnit(0, seed) * mp::math::kTwoPi) * kHillAmp;
    const float trend =
        mp::math::Sin(x / kTrendPeriod * mp::math::kTwoPi + HashUnit(1, seed) * mp::math::kTwoPi) * kTrendAmp;
    return kBaseY + noise + hill + trend;
}

[[nodiscard]] float TerrainSlope(float x, uint32_t seed) {
    return (TerrainAt(x + 2.0F, seed) - TerrainAt(x - 2.0F, seed)) * 0.25F;
}

// Parallax backdrop silhouette heights.
[[nodiscard]] float FarAt(float x, uint32_t seed, float amp) {
    return kBaseY - 180.0F + Noise1D(x * 0.55F, seed ^ 0xA5A5A5A5U) * amp + mp::math::Sin(x * 0.003F) * 60.0F;
}

[[nodiscard]] uint32_t HashU32(uint32_t a, uint32_t b) { return HashMix(a * 0x9E3779B1U ^ b * 0x85EBCA6BU); }

// Jerry can x positions are deterministic and unbounded.
[[nodiscard]] float CanX(uint32_t index, uint32_t seed) {
    return static_cast<float>(index) * kCanSpacingBase +
           static_cast<float>(HashU32(index, seed) % 560U) / 560.0F * kCanSpacingRange;
}

[[nodiscard]] float AngleDiff(float from, float to) {
    return mp::math::WrapAngle(to - from);
}

}  // namespace

int HillClimbAppMain() {
    micropixel::Application app;
    app.renderer().ConfigureDisplay({}).value();
    const micropixel::RendererInfo display = app.renderer().info();
    if (!display.raster_supported()) {
        app.log().Error("hillclimb: Host raster kernels required");
        return 1;
    }
    const uint32_t buffers = (display.physical_width() > 480U || display.physical_height() > 480U) ? 3U : 2U;
    auto created = app.renderer().CreateHostSurface(buffers, 1U);
    if (!created.has_value()) {
        app.log().Error("hillclimb: cannot allocate display buffers");
        return 1;
    }
    micropixel::HostSurface surface = static_cast<micropixel::HostSurface&&>(created.value());
    auto resources_created = app.renderer().CreateRasterResources();
    if (!resources_created.has_value()) {
        app.log().Error("hillclimb: cannot allocate raster resources");
        return 1;
    }
    micropixel::RasterResources resources = static_cast<micropixel::RasterResources&&>(resources_created.value());

    Palette palette{};
    palette.rgb565[kIdxWheel] = Rgb565(42.0F, 44.0F, 48.0F);
    palette.rgb565[kIdxBodyDark] = Rgb565(164.0F, 44.0F, 34.0F);
    palette.rgb565[kIdxBody] = Rgb565(222.0F, 66.0F, 48.0F);
    palette.rgb565[kIdxCabin] = Rgb565(56.0F, 96.0F, 176.0F);
    palette.rgb565[kIdxCan] = Rgb565(240.0F, 200.0F, 40.0F);
    palette.rgb565[kIdxCanDark] = Rgb565(186.0F, 148.0F, 22.0F);
    if (!resources.UploadLitPalette(kPaletteSlot, 1U, palette.rgb565).has_value()) {
        app.log().Error("hillclimb: palette upload failed");
        return 1;
    }

    micropixel::Timer frame_timer =
        app.timers().Every(micropixel::Duration::Microseconds(kFrameIntervalUs)).value();

    const int32_t view_w = static_cast<int32_t>(surface.buffer_width());
    const int32_t view_h = static_cast<int32_t>(surface.buffer_height());
    micropixel::Assert(view_w >= 480 && view_h >= 480, "hillclimb: requires a 480x480+ viewport");

    uint32_t best_m = app.storage().GetU32Or("best", 0U);

    // --- Game state -------------------------------------------------------
    uint32_t terrain_seed = 0U;
    float car_x = 0.0F;
    float car_y = 0.0F;
    float car_vx = 0.0F;
    float car_vy = 0.0F;
    float car_angle = 0.0F;
    float fuel = kFuelMax;
    float distance_m = 0.0F;
    float camera_x = 0.0F;
    float camera_y = 0.0F;
    bool throttle = false;
    bool running = false;
    OverReason over_reason = OverReason::kNone;
    bool hint_shown = true;
    uint64_t last_us = 0U;

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
        tone.release = micropixel::Duration::Milliseconds(20U);
        (void)app.audio().Play(tone);
    };

    const auto reset_game = [&]() {
        terrain_seed = app.random().Below(1000000U);
        car_x = 0.0F;
        car_y = TerrainAt(0.0F, terrain_seed) - kWheelRadius;
        car_vx = 0.0F;
        car_vy = 0.0F;
        car_angle = 0.0F;
        fuel = kFuelMax;
        distance_m = 0.0F;
        camera_x = car_x - static_cast<float>(view_w) * 0.38F;
        camera_y = car_y - static_cast<float>(view_h) * 0.62F;
        throttle = false;
        running = true;
        over_reason = OverReason::kNone;
        hint_shown = true;
    };

    reset_game();

    // --- Physics -----------------------------------------------------------
    const auto step = [&](float dt) {
        if (!running) {
            return;
        }
        const float cos_a = mp::math::Cos(car_angle);
        const float sin_a = mp::math::Sin(car_angle);

        // Wheel centres in world space.
        const float front_x = car_x + cos_a * kHalfWheelBase;
        const float front_y = car_y - sin_a * kHalfWheelBase;
        const float rear_x = car_x - cos_a * kHalfWheelBase;
        const float rear_y = car_y + sin_a * kHalfWheelBase;

        const float h_front = TerrainAt(front_x, terrain_seed);
        const float h_rear = TerrainAt(rear_x, terrain_seed);
        const bool grounded = front_y + kWheelRadius >= h_front || rear_y + kWheelRadius >= h_rear;

        // Forces: gravity plus engine along the body (nose direction is
        // (cos, -sin) in screen coords).
        float ax = 0.0F;
        float ay = kGravity;
        if (throttle) {
            ax += cos_a * kEngineAccel;
            ay -= sin_a * kEngineAccel;
        }
        car_vx += ax * dt;
        car_vy += ay * dt;

        const float drag = grounded ? kDrag + kRollFriction : kDrag;
        const float keep = mp::math::Max(1.0F - drag * dt, 0.0F);
        car_vx *= keep;
        car_vy *= keep;

        car_x += car_vx * dt;
        car_y += car_vy * dt;

        if (grounded) {
            // Align the car to the terrain under the wheels (spring-like
            // smoothing keeps it from snapping).
            for (uint32_t iter = 0U; iter < 2U; ++iter) {
                const float cf = mp::math::Cos(car_angle);
                const float pf_x = car_x + cf * kHalfWheelBase;
                const float pr_x = car_x - cf * kHalfWheelBase;
                const float yf = TerrainAt(pf_x, terrain_seed) - kWheelRadius;
                const float yr = TerrainAt(pr_x, terrain_seed) - kWheelRadius;
                const float y_target = (yf + yr) * 0.5F;
                const float dy = yf - yr;
                const float half_base = 2.0F * kHalfWheelBase;
                const float angle_target = -mp::math::Atan2(dy, mp::math::Sqrt(half_base * half_base - dy * dy));
                car_y += (y_target - car_y) * kGroundLerp;
                car_angle += (angle_target - car_angle) * kGroundLerp;
            }

            // Kill the velocity component that points into the ground and
            // measure the impact for crash detection.
            const float slope = TerrainSlope(car_x, terrain_seed);
            const float inv_len = 1.0F / mp::math::Sqrt(slope * slope + 1.0F);
            const float nx = slope * inv_len;
            const float ny = -1.0F * inv_len;
            const float vn = car_vx * nx + car_vy * ny;
            if (vn < 0.0F) {
                const float impact = -vn;
                car_vx -= vn * nx;
                car_vy -= vn * ny;
                if (impact > kCrashSpeed) {
                    over_reason = OverReason::kCrash;
                    running = false;
                    sfx(140U, 240U, 90U);
                    return;
                }
            }

            // Flip check: the car must not stay far from the local slope.
            const float ground_angle = -mp::math::Atan(slope);
            if (mp::math::Abs(AngleDiff(car_angle, ground_angle)) > kFlipLimit) {
                over_reason = OverReason::kCrash;
                running = false;
                sfx(140U, 240U, 90U);
                return;
            }
        } else {
            // Airborne: drift the nose back toward horizontal.
            const float ground_angle = -mp::math::Atan(TerrainSlope(car_x, terrain_seed));
            car_angle += AngleDiff(car_angle, ground_angle) * 2.0F * dt;
        }

        // Fuel burn and jerry can pickup.
        fuel -= kFuelBurn * dt;
        const uint32_t can_index = static_cast<uint32_t>(mp::math::Max(car_x, 0.0F) / kCanSpacingBase);
        const float can_x = CanX(can_index, terrain_seed);
        if (mp::math::Abs(car_x - can_x) < 40.0F) {
            const float can_y = TerrainAt(can_x, terrain_seed) - 46.0F;
            if (mp::math::Abs(car_y - can_y) < 70.0F) {
                if (fuel < kFuelMax) {
                    fuel = mp::math::Min(fuel + kFuelGain, kFuelMax);
                    sfx(880U, 60U, 80U);
                }
            }
        }
        if (fuel <= 0.0F) {
            fuel = 0.0F;
            over_reason = OverReason::kOutOfFuel;
            running = false;
            sfx(150U, 260U, 90U);
            return;
        }

        distance_m = mp::math::Max(distance_m, car_x / kPixelsPerMeter);

        // Camera follows with parallax damping and clamps to the terrain band.
        const float cam_target_x = car_x - static_cast<float>(view_w) * 0.38F;
        const float cam_target_y = car_y - static_cast<float>(view_h) * 0.62F;
        camera_x += (cam_target_x - camera_x) * 0.08F;
        camera_y += (cam_target_y - camera_y) * 0.10F;
        const float cam_min_y = kBaseY - 260.0F - static_cast<float>(view_h) * 0.75F;
        const float cam_max_y = kBaseY + 330.0F - static_cast<float>(view_h) * 0.35F;
        camera_y = mp::math::Clamp(camera_y, cam_min_y, cam_max_y);
    };

    // --- Rendering ----------------------------------------------------------
    const auto render = [&]() {
        uint32_t index = 0U;
        if (!surface.AcquireFree(index)) {
            return;
        }
        bool ok = true;
        const auto updated = surface.Update(index, [&](mp::RasterDrawList& list) {
            list.SetPalette(kPaletteSlot);

            // Sky with a subtle vertical gradient.
            ok = list.FillRect({0, 0, view_w, view_h}, kSky) && ok;
            ok = list.FillRect({0, 0, view_w, view_h / 3}, kSkyDeep) && ok;
            ok = list.FillRect({0, view_h * 5 / 6, view_w, view_h / 6}, kSkyPale) && ok;

            // Parallax mountains (two layers, columns 12 px apart).
            const int32_t far_step = 12;
            for (int32_t sx = -far_step; sx < view_w + far_step; sx += far_step) {
                const float world_x = (static_cast<float>(sx) + camera_x * 0.35F) * 1.0F;
                const float far_y = FarAt(world_x, terrain_seed, 70.0F) - camera_y * 0.55F;
                const float near_y = FarAt(world_x * 1.6F, terrain_seed ^ 0x3C3C3C3CU, 52.0F) - camera_y * 0.7F;
                const int32_t y0 = mp::math::Max(static_cast<int32_t>(far_y), 0);
                if (y0 < view_h) {
                    ok = list.FillRect({sx, y0, far_step, view_h - y0}, kMountainFar) && ok;
                }
                const int32_t y1 = mp::math::Max(static_cast<int32_t>(near_y), 0);
                if (y1 < view_h) {
                    ok = list.FillRect({sx, y1, far_step, view_h - y1}, kMountainNear) && ok;
                }
            }

            // Terrain columns with a bright top strip.
            const int32_t ground_step = 8;
            for (int32_t sx = -ground_step; sx < view_w + ground_step; sx += ground_step) {
                const float world_x = static_cast<float>(sx) + camera_x;
                const float surface_y = TerrainAt(world_x, terrain_seed) - camera_y;
                const int32_t y0 = mp::math::Max(static_cast<int32_t>(surface_y), 0);
                if (y0 < view_h) {
                    ok = list.FillRect({sx, y0, ground_step, view_h - y0}, kGround) && ok;
                    ok = list.FillRect({sx, y0, ground_step, 5}, kGroundTop) && ok;
                }
            }

            // Jerry cans in the camera window.
            {
                const float cam_lo = camera_x - 60.0F;
                const float cam_hi = camera_x + static_cast<float>(view_w) + 60.0F;
                const uint32_t can_start = static_cast<uint32_t>(mp::math::Max(cam_lo, 0.0F) / kCanSpacingBase);
                for (uint32_t i = can_start; i < can_start + 6U; ++i) {
                    const float can_x = CanX(i, terrain_seed);
                    if (can_x < cam_lo || can_x > cam_hi) {
                        continue;
                    }
                    const float can_y = TerrainAt(can_x, terrain_seed) - 46.0F;
                    const int32_t sx = static_cast<int32_t>(can_x - camera_x);
                    const int32_t sy = static_cast<int32_t>(can_y - camera_y);
                    if (sx < -40 || sx > view_w + 40) {
                        continue;
                    }
                    // Body.
                    const float quad[4][2] = {{static_cast<float>(sx) - 14.0F, static_cast<float>(sy) + 26.0F},
                                              {static_cast<float>(sx) - 14.0F, static_cast<float>(sy) - 4.0F},
                                              {static_cast<float>(sx) + 14.0F, static_cast<float>(sy) - 4.0F},
                                              {static_cast<float>(sx) + 14.0F, static_cast<float>(sy) + 26.0F}};
                    const mp::RasterVertex can_body[4] = {
                        mp::RasterVertex::At(quad[0][0], quad[0][1], 0, 0, 0),
                        mp::RasterVertex::At(quad[1][0], quad[1][1], 0, 0, 0),
                        mp::RasterVertex::At(quad[2][0], quad[2][1], 0, 0, 0),
                        mp::RasterVertex::At(quad[3][0], quad[3][1], 0, 0, 0)};
                    ok = list.FlatQuad(can_body, kIdxCan) && ok;
                    // Cap.
                    const mp::RasterVertex can_cap[4] = {
                        mp::RasterVertex::At(static_cast<float>(sx) - 16.0F, static_cast<float>(sy) - 6.0F, 0, 0, 0),
                        mp::RasterVertex::At(static_cast<float>(sx) - 16.0F, static_cast<float>(sy) - 16.0F, 0, 0, 0),
                        mp::RasterVertex::At(static_cast<float>(sx) + 16.0F, static_cast<float>(sy) - 16.0F, 0, 0, 0),
                        mp::RasterVertex::At(static_cast<float>(sx) + 16.0F, static_cast<float>(sy) - 6.0F, 0, 0, 0)};
                    ok = list.FlatQuad(can_cap, kIdxCanDark) && ok;
                }
            }

            // --- Car ---------------------------------------------------------
            const float cos_a = mp::math::Cos(car_angle);
            const float sin_a = mp::math::Sin(car_angle);
            const auto rotated = [&](float ux, float uy, float cx, float cy) {
                // Body local +x maps to the physical nose direction
                // (cos, -sin) so the car pitches up when climbing.
                return mp::RasterVertex::At(cx + cos_a * ux + sin_a * uy, cy - sin_a * ux + cos_a * uy, 0, 0, 0);
            };

            const float car_sx = car_x - camera_x;
            const float car_sy = car_y - camera_y;
            const float body_cy = car_sy - 6.0F;

            // Wheels: each is an octagon built from four quads.
            const float wheel_pts[8][2] = {{1.0F, 0.0F},   {0.7071F, 0.7071F}, {0.0F, 1.0F},   {-0.7071F, 0.7071F},
                                           {-1.0F, 0.0F},  {-0.7071F, -0.7071F}, {0.0F, -1.0F}, {0.7071F, -0.7071F}};
            const auto draw_wheel = [&](float cx, float cy) {
                for (uint32_t q = 0U; q < 4U; ++q) {
                    const uint32_t i0 = q * 2U;
                    const mp::RasterVertex corners[4] = {
                        mp::RasterVertex::At(cx + wheel_pts[i0][0] * kWheelRadius, cy + wheel_pts[i0][1] * kWheelRadius, 0, 0, 0),
                        mp::RasterVertex::At(cx + wheel_pts[i0 + 1U][0] * kWheelRadius,
                                             cy + wheel_pts[i0 + 1U][1] * kWheelRadius, 0, 0, 0),
                        mp::RasterVertex::At(cx + wheel_pts[(i0 + 2U) & 7U][0] * kWheelRadius,
                                             cy + wheel_pts[(i0 + 2U) & 7U][1] * kWheelRadius, 0, 0, 0),
                        mp::RasterVertex::At(cx + wheel_pts[(i0 + 3U) & 7U][0] * kWheelRadius,
                                             cy + wheel_pts[(i0 + 3U) & 7U][1] * kWheelRadius, 0, 0, 0)};
                    ok = list.FlatQuad(corners, kIdxWheel) && ok;
                }
                // Hub highlight.
                const mp::RasterVertex hub[4] = {
                    mp::RasterVertex::At(cx - 4.0F, cy - 4.0F, 0, 0, 0),
                    mp::RasterVertex::At(cx + 4.0F, cy - 4.0F, 0, 0, 0),
                    mp::RasterVertex::At(cx + 4.0F, cy + 4.0F, 0, 0, 0),
                    mp::RasterVertex::At(cx - 4.0F, cy + 4.0F, 0, 0, 0)};
                ok = list.FlatQuad(hub, kIdxBodyDark) && ok;
            };

            const float front_wheel_x = car_sx + cos_a * kHalfWheelBase;
            const float front_wheel_y = car_sy - sin_a * kHalfWheelBase;
            const float rear_wheel_x = car_sx - cos_a * kHalfWheelBase;
            const float rear_wheel_y = car_sy + sin_a * kHalfWheelBase;
            draw_wheel(front_wheel_x, front_wheel_y);
            draw_wheel(rear_wheel_x, rear_wheel_y);

            // Body: dark outline quad then a brighter inner quad.
            const mp::RasterVertex outline[4] = {
                rotated(-kBodyHalfWidth, -kBodyHalfHeight - 2.0F, car_sx, body_cy),
                rotated(kBodyHalfWidth, -kBodyHalfHeight - 2.0F, car_sx, body_cy),
                rotated(kBodyHalfWidth, kBodyHalfHeight + 2.0F, car_sx, body_cy),
                rotated(-kBodyHalfWidth, kBodyHalfHeight + 2.0F, car_sx, body_cy)};
            ok = list.FlatQuad(outline, kIdxBodyDark) && ok;
            const mp::RasterVertex body[4] = {rotated(-kBodyHalfWidth + 3.0F, -kBodyHalfHeight, car_sx, body_cy),
                                              rotated(kBodyHalfWidth - 3.0F, -kBodyHalfHeight, car_sx, body_cy),
                                              rotated(kBodyHalfWidth - 3.0F, kBodyHalfHeight, car_sx, body_cy),
                                              rotated(-kBodyHalfWidth + 3.0F, kBodyHalfHeight, car_sx, body_cy)};
            ok = list.FlatQuad(body, kIdxBody) && ok;

            // Cabin.
            const mp::RasterVertex cabin[4] = {
                rotated(-kCabinNose, -kBodyHalfHeight - 2.0F, car_sx, body_cy - 6.0F),
                rotated(kCabinNose, -kBodyHalfHeight - 2.0F, car_sx, body_cy - 6.0F),
                rotated(kCabinNose, -kBodyHalfHeight - 2.0F - kCabinTop, car_sx, body_cy - 6.0F),
                rotated(-kCabinNose, -kBodyHalfHeight - 2.0F - kCabinTop, car_sx, body_cy - 6.0F)};
            ok = list.FlatQuad(cabin, kIdxCabin) && ok;

            // --- HUD ---------------------------------------------------------
            Line dist_line;
            dist_line.AppendUint(static_cast<uint32_t>(distance_m));
            dist_line.Append(" m");
            ok = list.Text({18, 16}, dist_line.c_str(), kHudShadow, mp::SystemFont::kTitle) && ok;
            ok = list.Text({16, 14}, dist_line.c_str(), kHudText, mp::SystemFont::kTitle) && ok;
            Line best_line;
            best_line.Append("BEST ");
            best_line.AppendUint(best_m);
            best_line.Append(" m");
            ok = list.Text({18, 74}, best_line.c_str(), kHudDim, mp::SystemFont::kSmall) && ok;

            // Fuel bar (right).
            const int32_t fuel_x = view_w - 208;
            const int32_t fuel_y = 24;
            const int32_t fuel_w = 176;
            const int32_t fuel_h = 18;
            ok = list.FillRect({fuel_x - 2, fuel_y - 2, fuel_w + 4, fuel_h + 4}, kFuelBg) && ok;
            const float fuel_frac = fuel / kFuelMax;
            const int32_t fill_w = static_cast<int32_t>(static_cast<float>(fuel_w) * fuel_frac);
            if (fill_w > 0) {
                const mp::Color fuel_color = fuel_frac > 0.35F ? kFuelGood : kFuelLow;
                ok = list.FillRect({fuel_x, fuel_y, fill_w, fuel_h}, fuel_color) && ok;
            }
            ok = list.Text({view_w - 120, 52}, "FUEL", kHudDim, mp::SystemFont::kSmall) && ok;

            // Hint until the first acceleration.
            if (hint_shown && running) {
                ok = list.Text({view_w / 2 - 150, view_h - 64}, "HOLD TO ACCELERATE", kHudShadow, mp::SystemFont::kMedium) && ok;
                ok = list.Text({view_w / 2 - 152, view_h - 66}, "HOLD TO ACCELERATE", kHudText, mp::SystemFont::kMedium) && ok;
            }

            // Game-over overlay.
            if (!running) {
                const int32_t band_h = 240;
                const int32_t band_y = view_h / 2 - band_h / 2;
                ok = list.FillRect({0, band_y, view_w, band_h}, kOverlay, 216) && ok;
                const char* reason = over_reason == OverReason::kCrash ? "CRASH!" : "OUT OF FUEL";
                const auto title_measured = app.renderer().MeasureText(reason, mp::SystemFont::kTitle);
                if (title_measured.has_value()) {
                    const int32_t tx = (view_w - static_cast<int32_t>(title_measured->width)) / 2;
                    ok = list.Text({tx + 2, band_y + 26 + 2}, reason, kHudShadow, mp::SystemFont::kTitle) && ok;
                    ok = list.Text({tx, band_y + 26}, reason, kHudText, mp::SystemFont::kTitle) && ok;
                }
                Line score_line;
                score_line.Append("SCORE ");
                score_line.AppendUint(static_cast<uint32_t>(distance_m));
                score_line.Append(" m   BEST ");
                score_line.AppendUint(best_m);
                score_line.Append(" m");
                const auto sub_measured = app.renderer().MeasureText(score_line.c_str(), mp::SystemFont::kMedium);
                if (sub_measured.has_value()) {
                    const int32_t sx = (view_w - static_cast<int32_t>(sub_measured->width)) / 2;
                    ok = list.Text({sx, band_y + 96}, score_line.c_str(), kHudDim, mp::SystemFont::kMedium) && ok;
                }
                ok = list.Text({view_w / 2 - 110, band_y + 152}, "TAP TO RESTART", kHudText, mp::SystemFont::kMedium) && ok;
            }
        });
        if (!updated.has_value() || !ok || !surface.Present(index).has_value()) {
            app.log().Error("hillclimb: frame submission failed");
        }
    };

    // --- Main loop ---------------------------------------------------------
    float accumulator = 0.0F;
    app.Run([&](const micropixel::Event& event) {
        const uint64_t now_us = event.timestamp().microseconds();
        if (last_us == 0U) {
            last_us = now_us;
        }
        int32_t dt_ms = static_cast<int32_t>((now_us - last_us) / 1000U);
        if (dt_ms < 0) {
            dt_ms = 0;
        }
        if (dt_ms > 50) {
            dt_ms = 50;
        }
        last_us = now_us;

        if (event.type() == micropixel::EventType::kStop) {
            if (static_cast<uint32_t>(distance_m) > best_m) {
                best_m = static_cast<uint32_t>(distance_m);
                (void)app.storage().SetU32("best", best_m);
            }
            return micropixel::EventResult::kExit;
        }
        if (event.type() == micropixel::EventType::kResume) {
            render();
        }

        if (const micropixel::TouchEvent* touch = event.touch()) {
            if (touch->phase() == micropixel::TouchPhase::kDown) {
                if (running) {
                    throttle = true;
                    hint_shown = false;
                } else {
                    // Tap anywhere on the overlay restarts.
                    const int32_t ty = touch->y();
                    if (ty >= view_h / 2 - 120 && ty <= view_h / 2 + 120) {
                        reset_game();
                    }
                }
            } else if (touch->phase() == micropixel::TouchPhase::kUp) {
                throttle = false;
            }
        }

        if (running) {
            const float frame_dt = static_cast<float>(dt_ms) * 0.001F;
            accumulator += frame_dt;
            uint32_t steps = 0U;
            while (accumulator >= kPhysicsDt && steps < kMaxStepsPerFrame) {
                step(kPhysicsDt);
                accumulator -= kPhysicsDt;
                ++steps;
                if (!running) {
                    break;
                }
            }
            if (accumulator >= kPhysicsDt) {
                accumulator = 0.0F;
            }
            if (static_cast<uint32_t>(distance_m) > best_m) {
                best_m = static_cast<uint32_t>(distance_m);
            }
        }

        if (event.TimerFrom(frame_timer) != nullptr || event.type() == micropixel::EventType::kResume) {
            render();
        }
        return micropixel::EventResult::kContinue;
    });

    if (static_cast<uint32_t>(distance_m) > best_m) {
        best_m = static_cast<uint32_t>(distance_m);
        (void)app.storage().SetU32("best", best_m);
    }
    return 0;
}

}  // namespace hillclimb
