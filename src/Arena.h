// Animated terminal "arena" view. It listens to structured battle events and redraws the
// whole screen in place (ANSI escape codes): bars drain as hits land, damage numbers float
// next to the unit that was hit, and a short feed summarises what just happened.
#pragma once
#include "Battle.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct ArenaOptions {
    int delayMs = 180;     // base pause per action; <= 0 means "jump to the result" (final frame only)
    bool unicode = true;   // block characters for bars; false = plain ASCII
    bool color = true;     // ANSI colours
    bool silent = false;   // build frames but write nothing (used by tests)
};

class ArenaView : public BattleObserver {
public:
    static constexpr int kWidth = 118;   // columns needed
    static constexpr int kRows = 28;     // rows needed for a 5v5 battle plus one input line

    ArenaView(const Battle& battle, std::string labelA, std::string labelB, std::uint64_t seed, ArenaOptions opt);
    ~ArenaView() override;

    void begin();                       // clear the screen and hide the cursor
    void finish();                      // final frame with the result; restores the cursor
    void abort();                       // restore the terminal without drawing a result
    void showForInput(int activeUnit);  // redraw the frame and leave the cursor beneath it for a prompt

    void onEvent(const VisEvent& ev) override;

    // Exposed so tests can inspect exactly what would be drawn.
    std::vector<std::string> buildFrame() const;   // title, team headers, unit cards, separator
    std::vector<std::string> buildBottom() const;  // action banner + event feed
    void snapshot();                               // snap animated bar values to the real state

    bool instant() const { return opt_.delayMs <= 0; }

private:
    struct UnitVis {
        double hp = 0.0;
        double pr = 0.0;
        std::string floater;
        int floatKind = 0; // 0 none, 1 damage, 2 crit, 3 heal, 4 info (evade/resist), 5 defeat
    };
    struct FeedLine {
        std::string key;     // merge key for repeated lines ("" = never merge)
        int count = 1;
        std::string single;  // text when count == 1
        std::string multi;   // text when count > 1 (may contain "%d")
        int color = 0;       // 0 normal, 1 damage, 2 heal, 3 buff, 4 debuff, 5 info
    };

    const Battle& b_;
    std::string labelA_, labelB_;
    std::uint64_t seed_;
    ArenaOptions opt_;
    std::vector<UnitVis> v_;
    std::deque<FeedLine> feed_;
    std::string banner1_, banner2_;
    int active_ = -1;
    int target_ = -1;
    int hits_ = 0;
    double hitTotal_ = 0.0;
    bool cursorHidden_ = false;

    // drawing helpers
    std::string paint(const char* code, const std::string& s) const;
    std::string bar(double frac, int width) const;
    std::string pad(const std::string& s, int width) const;
    std::vector<std::string> unitCard(const BattleUnit& u, int width) const;
    std::string statusText(const BattleUnit& u, int maxChars) const;

    void render(bool withBottom);
    void write(const std::string& s) const;
    void pause(double factor) const;
    void clearFloaters();
    void syncShown(int except);
    void animate(int unit, double factor);
    void pushFeed(const std::string& key, const std::string& single, const std::string& multi, int color);
    void setFloater(int unit, const std::string& text, int kind);
};
