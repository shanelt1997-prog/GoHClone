#include "Arena.h"
#include "Ui.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <utility>

namespace {

const char* RST = "\x1b[0m";
const char* BOLD = "\x1b[1m";
const char* DIM = "\x1b[90m";
const char* RED = "\x1b[91m";
const char* GREEN = "\x1b[92m";
const char* YELLOW = "\x1b[93m";
const char* BLUE = "\x1b[94m";
const char* MAGENTA = "\x1b[95m";
const char* CYAN = "\x1b[96m";
const char* WHITE = "\x1b[97m";

constexpr int kLeftW = 57;
constexpr int kMidW = 3;
constexpr int kRightW = ArenaView::kWidth - kLeftW - kMidW;
constexpr int kNameW = 20;
constexpr int kFeedLines = 6;

std::string commas(double v) { return ui::withCommas(v); }

std::string fmtK(double v) {
    char buf[32];
    const double a = std::fabs(v);
    if (a < 1000.0) std::snprintf(buf, sizeof buf, "%.0f", v);
    else if (a < 100000.0) std::snprintf(buf, sizeof buf, "%.1fk", v / 1000.0);
    else if (a < 1000000.0) std::snprintf(buf, sizeof buf, "%.0fk", v / 1000.0);
    else std::snprintf(buf, sizeof buf, "%.2fM", v / 1000000.0);
    return buf;
}

std::string rightAlign(const std::string& s, int w) {
    if (static_cast<int>(s.size()) >= w) return s;
    return std::string(static_cast<size_t>(w) - s.size(), ' ') + s;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

// ============================================================= lifecycle ======

ArenaView::ArenaView(const Battle& battle, std::string labelA, std::string labelB, std::uint64_t seed, ArenaOptions opt)
    : b_(battle), labelA_(std::move(labelA)), labelB_(std::move(labelB)), seed_(seed), opt_(opt) {
    v_.resize(b_.units.size());
    snapshot();
    banner1_ = "Get ready...";
}

ArenaView::~ArenaView() {
    if (cursorHidden_ && !opt_.silent) write("\x1b[?25h");
}

void ArenaView::write(const std::string& s) const {
    if (opt_.silent) return;
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

void ArenaView::pause(double factor) const {
    if (opt_.silent || opt_.delayMs <= 0 || factor <= 0.0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(opt_.delayMs * factor)));
}

void ArenaView::begin() {
    snapshot();
    cursorHidden_ = true;
    write("\x1b[?25l\x1b[2J\x1b[H");
}

void ArenaView::abort() {
    if (cursorHidden_) write("\x1b[?25h");
    cursorHidden_ = false;
}

void ArenaView::finish() {
    snapshot();
    clearFloaters();
    const int w = b_.winner();
    if (w == 0) banner1_ = "TEAM A WINS!";
    else if (w == 1) banner1_ = "TEAM B WINS!";
    else banner1_ = "DRAW - the turn limit was reached";
    banner2_ = "Battle over after " + std::to_string(b_.turnsTaken) + " turns";
    active_ = target_ = -1;
    render(true);
    write("\x1b[?25h\n");
    cursorHidden_ = false;
}

void ArenaView::showForInput(int activeUnit) {
    snapshot();
    clearFloaters();
    active_ = activeUnit;
    target_ = -1;
    render(false);
    write("\x1b[?25h");
}

void ArenaView::snapshot() {
    v_.resize(b_.units.size());
    for (size_t i = 0; i < b_.units.size(); ++i) {
        v_[i].hp = b_.units[i].health;
        v_[i].pr = b_.units[i].protection;
    }
}

void ArenaView::syncShown(int except) {
    for (size_t i = 0; i < b_.units.size(); ++i) {
        if (static_cast<int>(i) == except) continue;
        v_[i].hp = b_.units[i].health;
        v_[i].pr = b_.units[i].protection;
    }
}

void ArenaView::clearFloaters() {
    for (UnitVis& u : v_) {
        u.floater.clear();
        u.floatKind = 0;
    }
}

void ArenaView::setFloater(int unit, const std::string& text, int kind) {
    if (unit < 0 || unit >= static_cast<int>(v_.size())) return;
    v_[static_cast<size_t>(unit)].floater = text;
    v_[static_cast<size_t>(unit)].floatKind = kind;
}

// ============================================================== drawing =======

std::string ArenaView::paint(const char* code, const std::string& s) const {
    if (!opt_.color || s.empty()) return s;
    return std::string(code) + s + RST;
}

std::string ArenaView::bar(double frac, int width) const {
    frac = std::clamp(frac, 0.0, 1.0);
    int filled = static_cast<int>(std::lround(frac * width));
    if (frac > 0.0 && filled == 0) filled = 1; // never show an alive bar as completely empty
    const std::string on = opt_.unicode ? "\xE2\x96\x88" : "#";   // full block
    const std::string off = opt_.unicode ? "\xE2\x96\x91" : ".";  // light shade
    std::string out;
    for (int i = 0; i < width; ++i) out += i < filled ? on : off;
    return out;
}

std::string ArenaView::pad(const std::string& s, int width) const {
    const int w = static_cast<int>(ui::visibleWidth(s));
    return w >= width ? s : s + std::string(static_cast<size_t>(width - w), ' ');
}

std::string ArenaView::statusText(const BattleUnit& u, int maxChars) const {
    // plain text first (so truncation is exact), colour applied per item afterwards
    std::string out;
    int used = 0;
    bool cut = false;
    for (const StatusInst& si : u.statuses) {
        if (si.fromAura) continue;
        std::string item = (si.def->kind == StatusKind::Debuff ? "-" : "+") + si.def->name;
        if (si.stacks > 1) item += " x" + std::to_string(si.stacks);
        if (si.remaining > 0) item += " " + std::to_string(si.remaining);
        const int need = static_cast<int>(item.size()) + (used ? 2 : 0);
        if (used + need > maxChars - 4) { cut = true; break; } // keep room for the " ..." marker
        if (used) out += "  ";
        out += paint(si.def->kind == StatusKind::Debuff ? RED : GREEN, item);
        used += need;
    }
    if (cut) out += paint(DIM, " ...");
    return out;
}

std::vector<std::string> ArenaView::unitCard(const BattleUnit& u, int width) const {
    const UnitVis& vis = v_[static_cast<size_t>(u.id)];
    std::string name = u.name.size() > static_cast<size_t>(kNameW) ? u.name.substr(0, kNameW) : u.name;
    char idbuf[16];
    std::snprintf(idbuf, sizeof idbuf, "#%-3d", u.id + 1);

    if (!u.alive) {
        std::string l1 = " " + std::string(idbuf) + pad(name, kNameW) + (u.isLeader ? "*" : " ") + "  -- DEFEATED --";
        std::string fl = vis.floater.empty() ? "" : paint(RED, vis.floater);
        return {pad(paint(DIM, l1), width), pad("    " + fl, width), std::string(static_cast<size_t>(width), ' ')};
    }

    const double mh = b_.maxHealth(u), mp = b_.maxProtection(u);
    const double hpFrac = vis.hp / mh;
    const double prFrac = mp > 0 ? vis.pr / mp : 0.0;
    const char* hpCol = hpFrac >= 0.6 ? GREEN : hpFrac >= 0.3 ? YELLOW : RED;

    std::string marker = " ";
    if (u.id == active_) marker = paint(YELLOW, ">");
    else if (u.id == target_) marker = paint(RED, "<");

    const char* teamCol = u.side == 0 ? CYAN : MAGENTA;
    std::string nameCell = pad(name, kNameW);
    nameCell = (u.id == active_) ? paint(BOLD, paint(WHITE, nameCell)) : paint(teamCol, nameCell);

    std::string l1 = marker + idbuf + nameCell + (u.isLeader ? paint(YELLOW, "*") : " ") + " " + "HP " +
                     paint(hpCol, bar(hpFrac, 12)) + " " + rightAlign(fmtK(vis.hp) + "/" + fmtK(mh), 11);

    const double tm = std::clamp(u.tm, 0.0, 1.0);
    char pct[16];
    std::snprintf(pct, sizeof pct, "%3d%%", static_cast<int>(std::lround(std::min(u.tm, 9.99) * 100)));
    std::string l2 = "    PR " + paint(BLUE, bar(prFrac, 10)) + " " +
                     rightAlign(mp > 0 ? fmtK(vis.pr) + "/" + fmtK(mp) : std::string("-"), 11) + "  TM " +
                     paint(YELLOW, bar(tm, 6)) + " " + pct;
    if (!vis.floater.empty()) {
        const char* c = vis.floatKind == 1 ? RED : vis.floatKind == 2 ? YELLOW : vis.floatKind == 3 ? GREEN
                        : vis.floatKind == 5 ? RED : CYAN;
        const int room = width - static_cast<int>(ui::visibleWidth(l2));
        const std::string f = vis.floater.size() > static_cast<size_t>(std::max(0, room - 1)) ? vis.floater.substr(0, static_cast<size_t>(std::max(0, room - 1))) : vis.floater;
        l2 += std::string(static_cast<size_t>(std::max(0, room - static_cast<int>(f.size()))), ' ') + paint(c, paint(BOLD, f));
    }

    std::string l3 = "    " + statusText(u, width - 4);
    return {pad(l1, width), pad(l2, width), pad(l3, width)};
}

std::vector<std::string> ArenaView::buildFrame() const {
    std::vector<std::string> out;

    // title bar
    std::string active = active_ >= 0 && active_ < static_cast<int>(b_.units.size()) ? b_.units[static_cast<size_t>(active_)].name : "";
    std::string title = " GoHClone  |  Turn " + std::to_string(b_.turnsTaken);
    if (!active.empty()) title += "  |  " + active;
    title += "  |  seed " + std::to_string(seed_) + " ";
    const int fill = std::max(0, kWidth - 2 - static_cast<int>(title.size()));
    out.push_back(paint(CYAN, "==" + title + std::string(static_cast<size_t>(fill), '=')));

    // team headers
    std::string ha = " TEAM A  " + labelA_;
    std::string hb = " TEAM B  " + labelB_;
    if (ha.size() > static_cast<size_t>(kLeftW)) ha = ha.substr(0, kLeftW);
    if (hb.size() > static_cast<size_t>(kRightW)) hb = hb.substr(0, kRightW);
    out.push_back(pad(paint(BOLD, paint(CYAN, ha)), kLeftW) + " | " + pad(paint(BOLD, paint(MAGENTA, hb)), kRightW));

    // unit cards, two columns
    std::vector<const BattleUnit*> sideA, sideB;
    for (const BattleUnit& u : b_.units) (u.side == 0 ? sideA : sideB).push_back(&u);
    const size_t rows = std::max(sideA.size(), sideB.size());
    for (size_t r = 0; r < rows; ++r) {
        std::vector<std::string> ca = r < sideA.size() ? unitCard(*sideA[r], kLeftW) : std::vector<std::string>(3, std::string(kLeftW, ' '));
        std::vector<std::string> cb = r < sideB.size() ? unitCard(*sideB[r], kRightW) : std::vector<std::string>(3, std::string(kRightW, ' '));
        for (size_t k = 0; k < 3; ++k) out.push_back(ca[k] + paint(DIM, " | ") + cb[k]);
    }
    out.push_back(paint(DIM, std::string(static_cast<size_t>(kWidth), '-')));
    return out;
}

std::vector<std::string> ArenaView::buildBottom() const {
    std::vector<std::string> out;
    out.push_back(" " + paint(BOLD, paint(WHITE, banner1_)));
    out.push_back(" " + paint(YELLOW, banner2_));
    // feed: oldest at the top, newest at the bottom, always exactly kFeedLines rows
    std::vector<std::string> lines;
    for (const FeedLine& f : feed_) {
        std::string text = f.count > 1 && !f.multi.empty() ? f.multi : f.single;
        const size_t p = text.find("%d");
        if (p != std::string::npos) text.replace(p, 2, std::to_string(f.count));
        if (text.size() > static_cast<size_t>(kWidth - 4)) text = text.substr(0, kWidth - 4);
        const char* c = f.color == 1 ? RED : f.color == 2 ? GREEN : f.color == 3 ? GREEN : f.color == 4 ? RED : f.color == 5 ? CYAN : WHITE;
        lines.push_back("  " + paint(c, text));
    }
    while (static_cast<int>(lines.size()) < kFeedLines) lines.insert(lines.begin(), "");
    out.insert(out.end(), lines.end() - kFeedLines, lines.end());
    return out;
}

void ArenaView::render(bool withBottom) {
    if (opt_.silent) return;
    std::string out = "\x1b[?25l\x1b[H";
    for (const std::string& l : buildFrame()) out += l + "\x1b[K\n";
    if (withBottom)
        for (const std::string& l : buildBottom()) out += l + "\x1b[K\n";
    out += "\x1b[J";
    write(out);
}

void ArenaView::animate(int unit, double factor) {
    if (unit < 0 || unit >= static_cast<int>(b_.units.size())) return;
    const BattleUnit& u = b_.units[static_cast<size_t>(unit)];
    UnitVis& vis = v_[static_cast<size_t>(unit)];
    const double hp0 = vis.hp, pr0 = vis.pr, hp1 = u.health, pr1 = u.protection;
    if (instant() || opt_.silent) {
        vis.hp = hp1;
        vis.pr = pr1;
        return;
    }
    const int frames = 6;
    for (int k = 1; k <= frames; ++k) {
        const double t = static_cast<double>(k) / frames;
        vis.hp = hp0 + (hp1 - hp0) * t;
        vis.pr = pr0 + (pr1 - pr0) * t;
        render(true);
        pause(factor / frames);
    }
}

void ArenaView::pushFeed(const std::string& key, const std::string& single, const std::string& multi, int color) {
    if (!key.empty() && !feed_.empty() && feed_.back().key == key) {
        feed_.back().count++;
        return;
    }
    FeedLine f;
    f.key = key;
    f.single = single;
    f.multi = multi;
    f.color = color;
    feed_.push_back(f);
    while (feed_.size() > 40) feed_.pop_front();
}

// ============================================================== events ========

void ArenaView::onEvent(const VisEvent& ev) {
    auto nameOf = [&](int id) -> std::string {
        return id >= 0 && id < static_cast<int>(b_.units.size()) ? b_.units[static_cast<size_t>(id)].name : std::string("?");
    };
    auto draw = [&](double factor) {
        if (instant() || opt_.silent) return;
        render(true);
        pause(factor);
    };

    switch (ev.kind) {
    case VisKind::TurnStart:
        active_ = ev.unit;
        target_ = -1;
        clearFloaters();
        syncShown(-1);
        banner1_ = nameOf(ev.unit) + "'s turn";
        banner2_.clear();
        hits_ = 0;
        hitTotal_ = 0.0;
        draw(0.35);
        break;

    case VisKind::Reaction:
        active_ = ev.unit;
        target_ = ev.other;
        banner1_ = nameOf(ev.unit) + (ev.flag ? " counterattacks " : " joins in against ") + nameOf(ev.other) + "!";
        banner2_.clear();
        hits_ = 0;
        hitTotal_ = 0.0;
        pushFeed("", nameOf(ev.unit) + (ev.flag ? " counterattacks!" : " assists!"), "", 5);
        syncShown(-1);
        draw(0.45);
        break;

    case VisKind::UseAbility:
        if (!ev.flag) { // in-turn action; reactions set their own banner
            active_ = ev.unit;
            target_ = ev.other >= 0 && ev.other != ev.unit ? ev.other : -1;
            banner1_ = nameOf(ev.unit) + " uses " + upper(ev.text) + (target_ >= 0 ? "  >>  " + nameOf(target_) : "");
            banner2_.clear();
            hits_ = 0;
            hitTotal_ = 0.0;
            pushFeed("", nameOf(ev.unit) + " uses " + ev.text + (target_ >= 0 ? " on " + nameOf(target_) : ""), "", 0);
            syncShown(-1);
            draw(0.6);
        }
        break;

    case VisKind::Damage: {
        syncShown(ev.unit);
        const double total = ev.a + ev.b;
        const bool dot = !ev.text.empty() && ev.text != "immune";
        if (ev.text == "immune") {
            setFloater(ev.unit, "IMMUNE", 4);
            pushFeed("", nameOf(ev.unit) + " is immune to damage", "", 5);
        } else {
            setFloater(ev.unit, "-" + commas(total) + (ev.flag ? "!" : ""), ev.flag ? 2 : 1);
            if (dot) {
                banner1_ = nameOf(ev.unit) + " suffers " + ev.text;
                banner2_ = "-" + commas(total) + " damage";
                pushFeed("", nameOf(ev.unit) + " takes " + commas(total) + " from " + ev.text, "", 1);
            } else {
                ++hits_;
                hitTotal_ += total;
                banner2_ = hits_ == 1 ? nameOf(ev.unit) + " takes " + commas(total) + " damage" + (ev.flag ? "  - CRITICAL HIT!" : "")
                                      : std::to_string(hits_) + " targets hit - " + commas(hitTotal_) + " total damage";
                pushFeed("", nameOf(ev.other) + " hits " + nameOf(ev.unit) + " for " + commas(total) + (ev.flag ? "  CRIT!" : ""), "", 1);
            }
        }
        animate(ev.unit, 0.55);
        pause(0.15);
        break;
    }

    case VisKind::Heal:
        syncShown(ev.unit);
        setFloater(ev.unit, "+" + commas(ev.a + ev.b), 3);
        pushFeed("", nameOf(ev.unit) + " recovers " + commas(ev.a + ev.b), "", 2);
        animate(ev.unit, 0.45);
        pause(0.1);
        break;

    case VisKind::Evade:
        setFloater(ev.unit, "EVADED", 4);
        pushFeed("", nameOf(ev.unit) + " evades " + nameOf(ev.other) + "'s attack", "", 5);
        draw(0.45);
        break;

    case VisKind::Resist:
        setFloater(ev.unit, "RESISTED", 4);
        pushFeed("", nameOf(ev.unit) + " resists" + (ev.text.empty() ? "" : " " + ev.text), "", 5);
        draw(0.45);
        break;

    case VisKind::StatusGain:
        // merge runs like "5 allies gain Speed Up" into one feed line
        pushFeed("gain:" + ev.text, nameOf(ev.unit) + " gains " + ev.text, "%d units gain " + ev.text, ev.flag ? 4 : 3);
        break;

    case VisKind::Defeat:
        syncShown(ev.unit);
        setFloater(ev.unit, "DEFEATED", 5);
        pushFeed("", nameOf(ev.unit) + " is defeated!", "", 1);
        banner2_ = nameOf(ev.unit) + " is defeated!";
        animate(ev.unit, 0.4);
        draw(0.9);
        break;

    case VisKind::Revive:
        syncShown(ev.unit);
        setFloater(ev.unit, "REVIVED", 3);
        pushFeed("", nameOf(ev.unit) + " is revived!", "", 2);
        animate(ev.unit, 0.4);
        draw(0.7);
        break;

    case VisKind::Stunned:
        banner1_ = nameOf(ev.unit) + " is stunned and loses the turn";
        pushFeed("", nameOf(ev.unit) + " is stunned and loses the turn", "", 4);
        draw(0.6);
        break;

    case VisKind::TurnEnd:
        syncShown(-1);
        draw(0.2);
        break;

    case VisKind::StatusLose:
    case VisKind::TurnMeter:
        break; // visible on the next redraw via the live bars / status line
    }
}
