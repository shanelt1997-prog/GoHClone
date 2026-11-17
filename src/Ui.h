// Terminal helpers, text rendering, and the interactive (human) controller.
#pragma once
#include "Ai.h"
#include "Battle.h"

#include <cstddef>
#include <string>

class ArenaView;

namespace ui {

// Detects/enables ANSI escape support. Returns true when cursor control and colours are usable.
// Set the environment variable GOHCLONE_FORCE_ANSI=1 to force it on (e.g. when piping output).
bool setup(bool wantColor);
bool ansiAvailable();
bool colorEnabled();
void setColor(bool on);        // ignored (stays off) when ANSI isn't available
bool unicodeEnabled();
void setUnicode(bool on);
void consoleSize(int& cols, int& rows); // best effort; 0/0 when unknown
void clearScreen();                     // clears the terminal (no-op without ANSI support)

std::size_t visibleWidth(const std::string& s); // printable columns, ignoring ANSI codes

void printState(const Battle& b);                // plain text snapshot of both teams
void inspect(const Battle& b, const BattleUnit& u);
void printResult(const Battle& b);
void printReport(const Battle& b, const std::string& labelA, const std::string& labelB);
std::string statusLine(const Battle& b, const BattleUnit& u);
std::string withCommas(double v);

} // namespace ui

struct QuitRequested {};

class HumanController : public Controller {
public:
    Decision decide(Battle& battle, BattleUnit& actor) override;
    bool autoMode = false;        // switched on by the 'a' command or end of input
    ArenaView* arena = nullptr;   // when set, prompts are drawn beneath the animated arena

private:
    Decision decideLog(Battle& battle, BattleUnit& actor);
    Decision decideArena(Battle& battle, BattleUnit& actor);
    AiController ai_;
};
