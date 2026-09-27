#include "game2048_app.hpp"

#include <stdint.h>

#include "sdk/micropixel.hpp"

namespace game2048 {

using Line = micropixel::FixedString<96U>;

namespace {

constexpr int32_t kCanvasWidth = 720;
constexpr int32_t kCanvasHeight = 720;
constexpr uint32_t kGridSize = 4U;
constexpr int32_t kBoardSize = 600;
constexpr int32_t kCellPitch = 148;
constexpr int32_t kCellSize = 140;
constexpr int32_t kBoardX = (kCanvasWidth - kBoardSize) / 2;
constexpr int32_t kBoardY = (kCanvasHeight - kBoardSize) / 2 + 24;
constexpr int32_t kSwipeThreshold = 40;

// Classic 2048 tile palette (RGB).
micropixel::Color TileColor(uint32_t value) {
    switch (value) {
        case 2U:
            return micropixel::Color::Rgb(238U, 228U, 218U);
        case 4U:
            return micropixel::Color::Rgb(237U, 224U, 200U);
        case 8U:
            return micropixel::Color::Rgb(242U, 177U, 121U);
        case 16U:
            return micropixel::Color::Rgb(245U, 149U, 99U);
        case 32U:
            return micropixel::Color::Rgb(246U, 124U, 95U);
        case 64U:
            return micropixel::Color::Rgb(246U, 94U, 59U);
        case 128U:
            return micropixel::Color::Rgb(237U, 207U, 114U);
        case 256U:
            return micropixel::Color::Rgb(237U, 204U, 97U);
        case 512U:
            return micropixel::Color::Rgb(237U, 200U, 80U);
        case 1024U:
            return micropixel::Color::Rgb(237U, 197U, 63U);
        default:
            return micropixel::Color::Rgb(237U, 194U, 46U);
    }
}

micropixel::Color TileTextColor(uint32_t value) {
    return (value <= 4U) ? micropixel::Color::Rgb(119U, 110U, 101U) : micropixel::Color::White();
}

micropixel::SystemFont TileFont(uint32_t value) {
    if (value >= 1024U) {
        return micropixel::SystemFont::kMedium;
    }
    if (value >= 128U) {
        return micropixel::SystemFont::kLarge;
    }
    return micropixel::SystemFont::kTitle;
}

enum class Direction : uint8_t { kUp, kDown, kLeft, kRight };

// Slides one line toward index 0 and merges equal neighbours.
// Returns the number of points earned.
uint32_t SlideLine(uint32_t* line, uint32_t count) {
    uint32_t compact[4]{};
    uint32_t compact_count = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        if (line[i] != 0U) {
            compact[compact_count++] = line[i];
        }
    }
    uint32_t out[4]{};
    uint32_t out_count = 0U;
    uint32_t score = 0U;
    uint32_t index = 0U;
    while (index < compact_count) {
        if (index + 1U < compact_count && compact[index] == compact[index + 1U]) {
            out[out_count++] = compact[index] * 2U;
            score += compact[index] * 2U;
            index += 2U;
        } else {
            out[out_count++] = compact[index];
            index += 1U;
        }
    }
    bool changed = false;
    for (uint32_t i = 0U; i < count; ++i) {
        if (line[i] != out[i]) {
            changed = true;
        }
        line[i] = out[i];
    }
    return changed ? score : 0U;
}

// Transposes the board in place; toggling transpose lets SlideLine serve
// horizontal and vertical slides with the same row-oriented code.
void Transpose(uint32_t (*board)[kGridSize]) {
    for (uint32_t row = 0U; row < kGridSize; ++row) {
        for (uint32_t column = row + 1U; column < kGridSize; ++column) {
            uint32_t temp = board[row][column];
            board[row][column] = board[column][row];
            board[column][row] = temp;
        }
    }
}

void ReverseRows(uint32_t (*board)[kGridSize]) {
    for (uint32_t row = 0U; row < kGridSize; ++row) {
        for (uint32_t a = 0U, b = kGridSize - 1U; a < b; ++a, --b) {
            uint32_t temp = board[row][a];
            board[row][a] = board[row][b];
            board[row][b] = temp;
        }
    }
}

}  // namespace

int Game2048AppMain() {
    micropixel::Application app;
    app.renderer()
        .ConfigureDisplay({.logical_size = {720U, 720U}, .scale_mode = micropixel::DisplayScaleMode::kExpand})
        .value();
    micropixel::Renderer renderer = app.renderer();
    micropixel::RendererInfo display = renderer.info();
    micropixel::Assert(display.width() >= static_cast<uint32_t>(kCanvasWidth) &&
                           display.height() >= static_cast<uint32_t>(kCanvasHeight),
                       "2048: requires a logical 720x720 viewport");

    micropixel::KVStore storage = app.storage();
    uint32_t best = storage.GetU32Or("best", 0U);

    uint32_t board[kGridSize][kGridSize]{};
    uint32_t score = 0U;
    bool game_over = false;
    bool won = false;

    micropixel::Random random = app.random();
    const auto spawn = [&]() {
        uint32_t empty[16]{};
        uint32_t empty_count = 0U;
        for (uint32_t row = 0U; row < kGridSize; ++row) {
            for (uint32_t column = 0U; column < kGridSize; ++column) {
                if (board[row][column] == 0U) {
                    empty[empty_count++] = static_cast<uint32_t>(row * kGridSize + column);
                }
            }
        }
        if (empty_count == 0U) {
            return;
        }
        const uint32_t slot = empty[random.Below(empty_count)];
        board[slot / kGridSize][slot % kGridSize] = (random.Below(10U) == 0U) ? 4U : 2U;
    };

    spawn();
    spawn();

    const auto can_move = [&]() {
        for (uint32_t row = 0U; row < kGridSize; ++row) {
            for (uint32_t column = 0U; column < kGridSize; ++column) {
                if (board[row][column] == 0U) {
                    return true;
                }
                if (column + 1U < kGridSize && board[row][column] == board[row][column + 1U]) {
                    return true;
                }
                if (row + 1U < kGridSize && board[row][column] == board[row + 1U][column]) {
                    return true;
                }
            }
        }
        return false;
    };

    const auto move = [&](Direction direction) {
        if (game_over) {
            return;
        }
        // Snapshot before sliding so movement can be detected by comparison.
        uint32_t before_board[kGridSize][kGridSize]{};
        for (uint32_t row = 0U; row < kGridSize; ++row) {
            for (uint32_t column = 0U; column < kGridSize; ++column) {
                before_board[row][column] = board[row][column];
            }
        }
        bool transposed = direction == Direction::kUp || direction == Direction::kDown;
        if (transposed) {
            Transpose(board);
        }
        if (direction == Direction::kDown || direction == Direction::kRight) {
            ReverseRows(board);
        }
        uint32_t gained = 0U;
        for (uint32_t row = 0U; row < kGridSize; ++row) {
            gained += SlideLine(board[row], kGridSize);
        }
        if (direction == Direction::kDown || direction == Direction::kRight) {
            ReverseRows(board);
        }
        if (transposed) {
            Transpose(board);
        }
        bool moved = false;
        for (uint32_t row = 0U; row < kGridSize; ++row) {
            for (uint32_t column = 0U; column < kGridSize; ++column) {
                if (board[row][column] != before_board[row][column]) {
                    moved = true;
                }
            }
        }
        if (!moved) {
            return;
        }
        score += gained;
        if (score > best) {
            best = score;
        }
        bool reached = false;
        for (uint32_t row = 0U; row < kGridSize; ++row) {
            for (uint32_t column = 0U; column < kGridSize; ++column) {
                if (board[row][column] >= 2048U) {
                    reached = true;
                }
            }
        }
        if (reached) {
            won = true;
        }
        spawn();
        game_over = !can_move();
    };

    // --- Scene graph ------------------------------------------------------
    micropixel::Scene scene = renderer.CreateScene(micropixel::Color::Rgb(250U, 247U, 238U)).value();
    micropixel::ContainerNode root =
        scene
            .CreateContainer({.clip = {0, 0, kCanvasWidth, kCanvasHeight},
                              .translation = {static_cast<int32_t>((static_cast<int32_t>(display.width()) -
                                                                     kCanvasWidth) /
                                                                    2),
                                              static_cast<int32_t>((static_cast<int32_t>(display.height()) -
                                                                     kCanvasHeight) /
                                                                    2)}})
            .value();

    (void)root.CreateLabel({40, 28}, "2048", micropixel::Color::Rgb(119U, 110U, 101U), micropixel::SystemFont::kTitle,
                           false)
        .value();
    (void)root.CreateLabel({kBoardX + kBoardSize - 190, 24}, "SCORE", micropixel::Color::Rgb(119U, 110U, 101U),
                           micropixel::SystemFont::kSmall, false)
        .value();
    micropixel::LabelNode score_value = root.CreateLabel({kBoardX + kBoardSize - 150, 56}, "0",
                                                         micropixel::Color::Rgb(119U, 110U, 101U),
                                                         micropixel::SystemFont::kLarge, false)
                                            .value();
    (void)root.CreateLabel({kBoardX + kBoardSize - 190, 108}, "BEST", micropixel::Color::Rgb(119U, 110U, 101U),
                           micropixel::SystemFont::kSmall, false)
        .value();
    micropixel::LabelNode best_value = root.CreateLabel({kBoardX + kBoardSize - 150, 140}, "0",
                                                        micropixel::Color::Rgb(119U, 110U, 101U),
                                                        micropixel::SystemFont::kLarge, false)
                                           .value();

    micropixel::RoundedRectNode empty_tiles[kGridSize][kGridSize]{};
    micropixel::LabelNode tile_labels[kGridSize][kGridSize]{};
    for (uint32_t row = 0U; row < kGridSize; ++row) {
        for (uint32_t column = 0U; column < kGridSize; ++column) {
            const int32_t x = kBoardX + static_cast<int32_t>(column) * kCellPitch;
            const int32_t y = kBoardY + static_cast<int32_t>(row) * kCellPitch;
            empty_tiles[row][column] =
                root.CreateRoundedRect({x, y, kCellSize, kCellSize},
                                       {.fill = micropixel::Color::Rgb(205U, 193U, 180U),
                                        .stroke = micropixel::Color::Rgb(205U, 193U, 180U),
                                        .radius = 10U,
                                        .stroke_width = 0U})
                    .value();
            tile_labels[row][column] = root.CreateLabel({x + kCellSize / 2, y + kCellSize / 2}, "",
                                                        micropixel::Color::White(), micropixel::SystemFont::kTitle, true)
                                           .value();
            tile_labels[row][column].SetVisible(false);
        }
    }

    micropixel::LabelNode status = root.CreateLabel({kBoardX, kBoardY + kBoardSize + 24}, "", micropixel::Color::Rgb(119U, 110U, 101U),
                                                    micropixel::SystemFont::kMedium, false)
                                       .value();
    (void)root.CreateLabel({kBoardX, kBoardY + kBoardSize + 64}, "SWIPE TO MOVE - TAP TO RESTART",
                           micropixel::Color::Rgb(205U, 193U, 180U), micropixel::SystemFont::kSmall, false)
        .value();

    // --- Rendering --------------------------------------------------------
    const auto render = [&]() {
        Line score_line;
        score_line.AppendUint(score);
        Line best_line;
        best_line.AppendUint(best);
        score_value.SetText(score_line.c_str());
        best_value.SetText(best_line.c_str());
        for (uint32_t row = 0U; row < kGridSize; ++row) {
            for (uint32_t column = 0U; column < kGridSize; ++column) {
                const uint32_t value = board[row][column];
                empty_tiles[row][column].SetFillColor(value == 0U ? micropixel::Color::Rgb(205U, 193U, 180U)
                                                                  : TileColor(value));
                if (value == 0U) {
                    tile_labels[row][column].SetVisible(false);
                } else {
                    Line value_line;
                    value_line.AppendUint(value);
                    tile_labels[row][column].SetText(value_line.c_str());
                    tile_labels[row][column].SetColor(TileTextColor(value));
                    tile_labels[row][column].SetFont(TileFont(value));
                    tile_labels[row][column].SetVisible(true);
                }
            }
        }
        if (game_over) {
            status.SetText("GAME OVER");
            status.SetColor(micropixel::Color::Rgb(246U, 94U, 59U));
        } else if (won) {
            status.SetText("YOU WIN! KEEP GOING");
            status.SetColor(micropixel::Color::Rgb(237U, 197U, 63U));
        } else {
            status.SetText("");
        }
        const auto result = renderer.Present(scene);
        micropixel::Assert(result.has_value(), "2048: scene present failed");
    };

    render();

    // --- Input ------------------------------------------------------------
    bool touching = false;
    int32_t touch_x = 0;
    int32_t touch_y = 0;

    app.Run([&](const micropixel::Event& event) {
        if (const micropixel::TouchEvent* touch = event.touch()) {
            if (touch->phase() == micropixel::TouchPhase::kDown) {
                touching = true;
                touch_x = touch->x();
                touch_y = touch->y();
            } else if (touch->phase() == micropixel::TouchPhase::kUp) {
                if (touching) {
                    const int32_t dx = touch->x() - touch_x;
                    const int32_t dy = touch->y() - touch_y;
                    const int32_t adx = dx < 0 ? -dx : dx;
                    const int32_t ady = dy < 0 ? -dy : dy;
                    if (adx < kSwipeThreshold && ady < kSwipeThreshold) {
                        // Tap: restart when the game is over.
                        if (game_over) {
                            for (uint32_t row = 0U; row < kGridSize; ++row) {
                                for (uint32_t column = 0U; column < kGridSize; ++column) {
                                    board[row][column] = 0U;
                                }
                            }
                            score = 0U;
                            game_over = false;
                            won = false;
                            spawn();
                            spawn();
                            render();
                        }
                    } else if (adx > ady) {
                        move(dx > 0 ? Direction::kRight : Direction::kLeft);
                        render();
                    } else {
                        move(dy > 0 ? Direction::kDown : Direction::kUp);
                        render();
                    }
                }
                touching = false;
            }
        } else if (event.type() == micropixel::EventType::kResume) {
            render();
        }
    });

    micropixel::Line final_line;
    final_line.Append("2048: session finished BEST ");
    final_line.AppendUint(best);
    app.log().Info(final_line.c_str());
    if (best > 0U) {
        (void)storage.SetU32("best", best);
    }
    return 0;
}

}  // namespace game2048
