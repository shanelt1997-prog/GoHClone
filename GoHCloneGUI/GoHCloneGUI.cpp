#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include "Ai.h"
#include "Battle.h"
#include "Defs.h"
#include "Json.h"
#include "Rules.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")

namespace fs = std::filesystem;

namespace {
constexpr wchar_t kClassName[] = L"GoHCloneDesktopWindow";
constexpr int ID_NAV_HOME = 100, ID_NAV_UNIT = 101, ID_NAV_STATUS = 102, ID_NAV_SIM = 103;
constexpr int ID_SAVE_UNIT = 200, ID_CLEAR_UNIT = 201, ID_NAME = 202, ID_UNIT_ID = 203;
constexpr int ID_STATUS_FILTER = 300, ID_STATUS_SEARCH = 301, ID_STATUS_LIST = 302, ID_STATUS_DETAIL = 303;
constexpr int ID_SIM_RUN = 400, ID_SIM_A = 401, ID_SIM_B = 402, ID_SIM_RUNS = 403, ID_SIM_SEED = 404;
constexpr int ID_VALIDATE = 500, ID_HOME_STATUS = 501, ID_HOME_UNITS = 502, ID_HOME_TEAMS = 503, ID_HOME_DATA = 504;
constexpr int ID_BASIC_NAME = 600, ID_BASIC_TYPE = 601, ID_BASIC_MULT = 602;
constexpr int ID_SPECIAL_NAME = 610, ID_SPECIAL_TYPE = 611, ID_SPECIAL_MULT = 612, ID_SPECIAL_CD = 613;
constexpr int ID_SPECIAL_STATUS = 614, ID_SPECIAL_STATUS_TARGET = 615, ID_SPECIAL_CHANCE = 616, ID_SPECIAL_DURATION = 617;
constexpr int ID_SPECIAL_BONUS = 618;
constexpr UINT WM_SIM_DONE = WM_APP + 10;

HWND gMain = nullptr;
HFONT gFont = nullptr;
int gPage = 0;
std::vector<HWND> gPageControls;
fs::path gDataDir;
fs::path gExeDir;
std::unique_ptr<Content> gContent;
std::vector<std::string> gFilteredStatusIds;
std::vector<std::pair<std::string, std::string>> gTeams;
std::vector<std::pair<int, HWND>> gStatEdits;
HWND gName = nullptr, gUnitId = nullptr, gLevel = nullptr, gTags = nullptr;
HWND gBasicName = nullptr, gBasicType = nullptr, gBasicMult = nullptr;
HWND gSpecialName = nullptr, gSpecialType = nullptr, gSpecialMult = nullptr, gSpecialCd = nullptr;
HWND gSpecialStatus = nullptr, gSpecialStatusTarget = nullptr, gSpecialChance = nullptr, gSpecialDuration = nullptr;
HWND gSpecialBonus = nullptr;
HWND gStatusFilter = nullptr, gStatusSearch = nullptr, gStatusList = nullptr, gStatusDetail = nullptr;
HWND gSimA = nullptr, gSimB = nullptr, gSimRuns = nullptr, gSimSeed = nullptr, gSimOutput = nullptr, gSimButton = nullptr;
HWND gHomeStatus = nullptr, gHomeUnits = nullptr, gHomeTeams = nullptr, gHomeData = nullptr;
std::thread gWorker;
bool gBusy = false;

struct StatField { const wchar_t* label; const char* key; };
const StatField kStats[] = {
    {L"Health", "health"}, {L"Protection", "protection"}, {L"Speed", "speed"},
    {L"Physical damage", "physical_damage"}, {L"Special damage", "special_damage"},
    {L"Armor (%)", "armor_pct"}, {L"Resistance (%)", "resistance_pct"},
    {L"Physical crit (%)", "phys_crit_pct"}, {L"Special crit (%)", "spec_crit_pct"},
    {L"Critical damage (%)", "crit_damage_pct"}, {L"Potency (%)", "potency_pct"},
    {L"Tenacity (%)", "tenacity_pct"}, {L"Accuracy (%)", "accuracy_pct"},
    {L"Critical avoidance (%)", "crit_avoid_pct"}, {L"Dodge (%)", "dodge_pct"},
    {L"Deflection (%)", "deflection_pct"}, {L"Health steal (%)", "health_steal_pct"},
    {L"Armor penetration", "armor_pen"}, {L"Resistance penetration", "resistance_pen"}
};

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::wstring textOf(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}
void setText(HWND h, const std::wstring& s) { if (h) SetWindowTextW(h, s.c_str()); }
std::wstring trimW(std::wstring s) {
    while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}
std::string trim(std::string s) {
    auto ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && ws(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}
std::string jsonEscape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char b[7]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += static_cast<char>(c);
        }
    }
    return out;
}
std::string number(double x) {
    std::ostringstream s;
    s << std::setprecision(12) << x;
    return s.str();
}
bool parseNumber(const std::wstring& s, double& v) {
    const std::wstring t = trimW(s);
    if (t.empty()) { v = 0; return true; }
    wchar_t* end = nullptr;
    v = wcstod(t.c_str(), &end);
    return end != t.c_str() && *end == L'\0' && std::isfinite(v) && v >= 0;
}
std::string slug(const std::wstring& name) {
    std::string out;
    bool underscore = false;
    for (wchar_t c : name) {
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) {
            out += static_cast<char>(c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c);
            underscore = false;
        } else if (!out.empty() && !underscore) { out += '_'; underscore = true; }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}
std::vector<std::string> splitTags(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        part = trim(part);
        if (!part.empty()) out.push_back(part);
    }
    return out;
}
fs::path findDataFolder() {
    std::error_code ec;
    fs::path p = gExeDir;
    fs::path best;
    for (int i = 0; i < 9 && !p.empty(); ++i) {
        fs::path candidate = p / "data";
        if (fs::exists(candidate / "content.json", ec)) best = candidate;
        p = p.parent_path();
    }
    p = fs::current_path(ec);
    for (int i = 0; i < 6 && !p.empty(); ++i) {
        fs::path candidate = p / "data";
        if (fs::exists(candidate / "content.json", ec)) best = candidate;
        p = p.parent_path();
    }
    return best;
}
void addPage(HWND h) { gPageControls.push_back(h); if (gFont) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE); }
HWND control(DWORD ex, const wchar_t* cls, const std::wstring& label, DWORD style, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(ex, cls, label.c_str(), WS_CHILD | WS_VISIBLE | style, x, y, w, h, gMain,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    addPage(c);
    return c;
}
HWND label(const std::wstring& s, int x, int y, int w = 140, int h = 22) {
    return control(0, L"STATIC", s, SS_LEFT, 0, x, y, w, h);
}
HWND edit(const std::wstring& s, int id, int x, int y, int w = 180) {
    return control(WS_EX_CLIENTEDGE, L"EDIT", s, ES_AUTOHSCROLL | WS_TABSTOP, id, x, y, w, 25);
}
HWND button(const std::wstring& s, int id, int x, int y, int w = 140, int h = 32) {
    return control(0, L"BUTTON", s, BS_PUSHBUTTON | WS_TABSTOP, id, x, y, w, h);
}
HWND combo(int id, int x, int y, int w = 180) {
    return control(0, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, id, x, y, w, 220);
}
void addCombo(HWND h, const std::wstring& s) { SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str())); }
void clearPage() {
    for (HWND h : gPageControls) if (IsWindow(h)) DestroyWindow(h);
    gPageControls.clear();
    gStatEdits.clear();
}
std::wstring dataPathText() { return gDataDir.empty() ? L"Data folder not found" : widen(gDataDir.wstring()); }

void refreshContent() {
    if (gDataDir.empty()) throw std::runtime_error("Could not find data/content.json. Keep the data folder beside the application or under the project folder.");
    gContent = std::make_unique<Content>(loadContent((gDataDir / "content.json").string()));
}
void updateHome() {
    if (!gHomeStatus || !gContent) return;
    size_t buffs = 0, debuffs = 0, neutral = 0;
    for (const auto& kv : gContent->statuses) {
        if (kv.second.kind == StatusKind::Buff) ++buffs;
        else if (kv.second.kind == StatusKind::Debuff) ++debuffs;
        else ++neutral;
    }
    setText(gHomeStatus, widen(std::to_string(buffs) + " buffs  |  " + std::to_string(debuffs) + " debuffs  |  " +
                               std::to_string(neutral) + " neutral"));
    setText(gHomeUnits, widen(std::to_string(gContent->units.size()) + " units loaded"));
    setText(gHomeTeams, widen(std::to_string(gContent->teams.size()) + " demo teams loaded"));
    setText(gHomeData, dataPathText());
}
void updateUnitId() {
    if (!gName || !gUnitId) return;
    setText(gUnitId, widen(slug(textOf(gName))));
}
void clearUnitForm() {
    if (!gName) return;
    setText(gName, L""); setText(gUnitId, L""); setText(gTags, L"");
    setText(gLevel, L"85");
    for (auto& p : gStatEdits) setText(p.second, L"");
    setText(gBasicName, L"Basic Attack"); setText(gBasicMult, L"1.0");
    SendMessageW(gBasicType, CB_SETCURSEL, 0, 0);
    setText(gSpecialName, L""); setText(gSpecialMult, L"1.5"); setText(gSpecialCd, L"3");
    setText(gSpecialChance, L"100"); setText(gSpecialDuration, L"2");
    SendMessageW(gSpecialType, CB_SETCURSEL, 0, 0);
    SendMessageW(gSpecialStatus, CB_SETCURSEL, 0, 0);
    SendMessageW(gSpecialStatusTarget, CB_SETCURSEL, 0, 0);
    SendMessageW(gSpecialBonus, BM_SETCHECK, BST_UNCHECKED, 0);
}
void createHomePage() {
    label(L"GoHClone Desktop", 30, 92, 500, 42);
    label(L"Manage your roster, review status effects and run balance simulations without using the terminal.", 30, 140, 1040, 28);
    label(L"STATUS LIBRARY", 30, 205, 250, 22);
    gHomeStatus = label(L"", 30, 232, 700, 28);
    label(L"ROSTER", 30, 290, 250, 22);
    gHomeUnits = label(L"", 30, 317, 500, 28);
    label(L"TEAMS", 30, 360, 250, 22);
    gHomeTeams = label(L"", 30, 387, 500, 28);
    label(L"DATA FOLDER", 30, 430, 250, 22);
    gHomeData = label(L"", 30, 457, 1050, 28);
    button(L"Add a new unit", ID_NAV_UNIT, 30, 525, 180, 40);
    button(L"Browse buffs and debuffs", ID_NAV_STATUS, 230, 525, 220, 40);
    button(L"Run a battle simulation", ID_NAV_SIM, 470, 525, 220, 40);
    button(L"Validate game data", ID_VALIDATE, 30, 600, 180, 34);
    label(L"Your original terminal application is still included in the solution as a fallback.", 30, 665, 1000, 24);
    updateHome();
}
void createUnitPage() {
    label(L"Add a unit", 25, 82, 300, 34);
    label(L"Enter the values shown on the unit's stat screen. Percentage fields are entered as percentages (for example, 105.2).", 25, 116, 1080, 26);
    label(L"UNIT DETAILS", 25, 153, 180, 20);
    label(L"Name", 25, 181, 60); gName = edit(L"", ID_NAME, 85, 178, 220);
    label(L"Unit ID", 330, 181, 65); gUnitId = edit(L"", ID_UNIT_ID, 395, 178, 190); SendMessageW(gUnitId, EM_SETREADONLY, TRUE, 0);
    label(L"Level", 610, 181, 55); gLevel = edit(L"85", 665, 178, 60);
    label(L"Tags (comma separated)", 750, 181, 170); gTags = edit(L"", 204, 923, 178, 190);
    label(L"STATS", 25, 222, 180, 20);
    control(0, L"BUTTON", L"Stats as displayed in-game", BS_GROUPBOX, 0, 18, 242, 1120, 298);
    for (size_t i = 0; i < std::size(kStats); ++i) {
        int col = static_cast<int>(i / 5), row = static_cast<int>(i % 5);
        int x = 34 + col * 278;
        int y = 270 + row * 48;
        label(kStats[i].label, x, y, 150, 22);
        HWND e = edit(L"", 700 + static_cast<int>(i), x + 150, y - 3, 108);
        gStatEdits.push_back({static_cast<int>(i), e});
    }
    control(0, L"BUTTON", L"BASIC ABILITY", BS_GROUPBOX, 0, 18, 550, 1120, 92);
    label(L"Name", 34, 580, 50); gBasicName = edit(L"Basic Attack", ID_BASIC_NAME, 85, 577, 210);
    label(L"Damage type", 320, 580, 95); gBasicType = combo(ID_BASIC_TYPE, 415, 577, 150);
    addCombo(gBasicType, L"Physical"); addCombo(gBasicType, L"Special"); addCombo(gBasicType, L"True damage");
    SendMessageW(gBasicType, CB_SETCURSEL, 0, 0);
    label(L"Damage multiplier", 595, 580, 120); gBasicMult = edit(L"1.0", ID_BASIC_MULT, 715, 577, 90);
    control(0, L"BUTTON", L"OPTIONAL SPECIAL ABILITY", BS_GROUPBOX, 0, 18, 652, 1120, 120);
    label(L"Name", 34, 682, 50); gSpecialName = edit(L"", ID_SPECIAL_NAME, 85, 679, 205);
    label(L"Damage type", 305, 682, 95); gSpecialType = combo(ID_SPECIAL_TYPE, 400, 679, 130);
    addCombo(gSpecialType, L"Physical"); addCombo(gSpecialType, L"Special"); addCombo(gSpecialType, L"True damage");
    SendMessageW(gSpecialType, CB_SETCURSEL, 0, 0);
    label(L"Multiplier", 545, 682, 75); gSpecialMult = edit(L"1.5", ID_SPECIAL_MULT, 620, 679, 75);
    label(L"Cooldown", 715, 682, 70); gSpecialCd = edit(L"3", ID_SPECIAL_CD, 785, 679, 55);
    label(L"Status effect", 34, 722, 90); gSpecialStatus = combo(ID_SPECIAL_STATUS, 124, 719, 210);
    label(L"Apply to", 350, 722, 65); gSpecialStatusTarget = combo(ID_SPECIAL_STATUS_TARGET, 415, 719, 145);
    addCombo(gSpecialStatusTarget, L"Enemy hit"); addCombo(gSpecialStatusTarget, L"Self"); addCombo(gSpecialStatusTarget, L"All allies"); addCombo(gSpecialStatusTarget, L"All enemies");
    SendMessageW(gSpecialStatusTarget, CB_SETCURSEL, 0, 0);
    label(L"Chance (%)", 580, 722, 80); gSpecialChance = edit(L"100", ID_SPECIAL_CHANCE, 660, 719, 60);
    label(L"Duration", 740, 722, 65); gSpecialDuration = edit(L"2", ID_SPECIAL_DURATION, 805, 719, 55);
    gSpecialBonus = control(0, L"BUTTON", L"Also grants this unit a bonus turn", BS_AUTOCHECKBOX | WS_TABSTOP, ID_SPECIAL_BONUS, 34, 753, 280, 23);
    button(L"Save unit", ID_SAVE_UNIT, 790, 805, 145, 38);
    button(L"Clear form", ID_CLEAR_UNIT, 950, 805, 145, 38);
    if (gContent) {
        addCombo(gSpecialStatus, L"None");
        for (const auto& id : gContent->statusOrder) {
            const auto it = gContent->statuses.find(id);
            if (it != gContent->statuses.end())
                addCombo(gSpecialStatus, widen(it->second.name + " [" + id + "]"));
        }
        SendMessageW(gSpecialStatus, CB_SETCURSEL, 0, 0);
    }
    updateUnitId();
}
void populateStatusList() {
    if (!gStatusList || !gContent) return;
    SendMessageW(gStatusList, LB_RESETCONTENT, 0, 0);
    gFilteredStatusIds.clear();
    const int f = static_cast<int>(SendMessageW(gStatusFilter, CB_GETCURSEL, 0, 0));
    const std::string search = narrow(textOf(gStatusSearch));
    for (const auto& id : gContent->statusOrder) {
        const auto it = gContent->statuses.find(id);
        if (it == gContent->statuses.end()) continue;
        const StatusDef& st = it->second;
        if (f == 1 && st.kind != StatusKind::Buff) continue;
        if (f == 2 && st.kind != StatusKind::Debuff) continue;
        if (f == 3 && st.kind != StatusKind::Neutral) continue;
        std::string hay = st.name + " " + st.id + " " + st.description;
        std::string lower = hay;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string q = search;
        std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!q.empty() && lower.find(q) == std::string::npos) continue;
        std::string kind = st.kind == StatusKind::Buff ? "BUFF" : st.kind == StatusKind::Debuff ? "DEBUFF" : "NEUTRAL";
        const std::wstring row = widen(kind + "  |  " + st.name + "  [" + st.id + "]");
        LRESULT n = SendMessageW(gStatusList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
        SendMessageW(gStatusList, LB_SETITEMDATA, static_cast<WPARAM>(n), static_cast<LPARAM>(gFilteredStatusIds.size()));
        gFilteredStatusIds.push_back(id);
    }
    if (!gFilteredStatusIds.empty()) SendMessageW(gStatusList, LB_SETCURSEL, 0, 0);
    updateStatusDetail();
}
void updateStatusDetail() {
    if (!gStatusList || !gStatusDetail || !gContent) return;
    int n = static_cast<int>(SendMessageW(gStatusList, LB_GETCURSEL, 0, 0));
    if (n < 0 || static_cast<size_t>(n) >= gFilteredStatusIds.size()) { setText(gStatusDetail, L"Select a status to see its details."); return; }
    const auto it = gContent->statuses.find(gFilteredStatusIds[static_cast<size_t>(n)]);
    if (it == gContent->statuses.end()) return;
    const StatusDef& s = it->second;
    const std::string kind = s.kind == StatusKind::Buff ? "Buff" : s.kind == StatusKind::Debuff ? "Debuff" : "Neutral";
    std::ostringstream out;
    out << s.name << "\r\n"
        << "ID: " << s.id << "\r\n"
        << "Type: " << kind << "\r\n"
        << "Default duration: " << s.duration << (s.duration == -1 ? " (permanent)" : " turns") << "\r\n"
        << "Maximum stacks: " << s.maxStacks << "\r\n"
        << "Dispellable: " << (s.dispellable ? "Yes" : "No") << "\r\n\r\n"
        << s.description;
    setText(gStatusDetail, widen(out.str()));
}
void createStatusPage() {
    label(L"Status library", 25, 82, 300, 34);
    label(L"Browse the effects currently defined in your project. Select one to see its rules and ID.", 25, 116, 1000, 24);
    gStatusFilter = combo(ID_STATUS_FILTER, 25, 160, 170);
    addCombo(gStatusFilter, L"All statuses"); addCombo(gStatusFilter, L"Buffs only");
    addCombo(gStatusFilter, L"Debuffs only"); addCombo(gStatusFilter, L"Neutral only");
    SendMessageW(gStatusFilter, CB_SETCURSEL, 0, 0);
    label(L"Search", 220, 164, 60); gStatusSearch = edit(L"", ID_STATUS_SEARCH, 280, 158, 260);
    gStatusList = control(WS_EX_CLIENTEDGE, L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, ID_STATUS_LIST, 25, 205, 505, 590);
    label(L"STATUS DETAILS", 565, 205, 250, 22);
    gStatusDetail = control(WS_EX_CLIENTEDGE, L"EDIT", L"Select a status to see its details.", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, ID_STATUS_DETAIL, 565, 235, 545, 300);
    populateStatusList();
}
void createSimPage() {
    label(L"Battle simulator", 25, 82, 300, 34);
    label(L"Choose existing team IDs or enter comma-separated unit IDs. The first unit in a custom team is its leader.", 25, 116, 1080, 24);
    label(L"Team A", 25, 165, 60); gSimA = edit(L"concord_strike", ID_SIM_A, 85, 160, 260);
    label(L"Team B", 380, 165, 60); gSimB = edit(L"syndicate_raiders", ID_SIM_B, 440, 160, 260);
    label(L"Battles", 730, 165, 60); gSimRuns = edit(L"100", ID_SIM_RUNS, 790, 160, 70);
    label(L"Seed (blank = random)", 885, 165, 150); gSimSeed = edit(L"", ID_SIM_SEED, 1035, 160, 75);
    gSimButton = button(L"Run simulation", ID_SIM_RUN, 25, 210, 160, 38);
    button(L"Validate data", ID_VALIDATE, 205, 210, 150, 38);
    label(L"RESULTS", 25, 275, 160, 22);
    gSimOutput = control(WS_EX_CLIENTEDGE, L"EDIT", L"Run a simulation to see the results here.", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL, 0, 25, 305, 1085, 480);
}
void showPage(int page) {
    gPage = page;
    clearPage();
    try { refreshContent(); }
    catch (const std::exception& e) {
        MessageBoxW(gMain, widen(e.what()).c_str(), L"Data folder", MB_ICONWARNING | MB_OK);
    }
    switch (gPage) {
    case 0: createHomePage(); break;
    case 1: createUnitPage(); break;
    case 2: createStatusPage(); break;
    case 3: createSimPage(); break;
    }
}
std::string damageType(HWND h) {
    int n = static_cast<int>(SendMessageW(h, CB_GETCURSEL, 0, 0));
    return n == 1 ? "ability_power" : n == 2 ? "unmitigable" : "attack_damage";
}
std::string selectedStatusId() {
    int n = static_cast<int>(SendMessageW(gSpecialStatus, CB_GETCURSEL, 0, 0));
    if (n <= 0) return {};
    std::wstring s(512, L'\0');
    LRESULT len = SendMessageW(gSpecialStatus, CB_GETLBTEXT, n, reinterpret_cast<LPARAM>(s.data()));
    if (len < 0) return {};
    s.resize(static_cast<size_t>(len));
    size_t a = s.find_last_of(L'['), b = s.find_last_of(L']');
    if (a == std::wstring::npos || b == std::wstring::npos || b <= a) return {};
    return narrow(s.substr(a + 1, b - a - 1));
}
void saveUnit() {
    const std::string name = narrow(trimW(textOf(gName)));
    const std::string id = slug(textOf(gName));
    if (name.empty() || id.empty()) { MessageBoxW(gMain, L"Enter a unit name first.", L"Missing name", MB_ICONWARNING); return; }
    int level = 85;
    try { level = std::stoi(narrow(trimW(textOf(gLevel)))); }
    catch (...) { MessageBoxW(gMain, L"Level must be a whole number from 1 to 100.", L"Invalid level", MB_ICONWARNING); return; }
    if (level < 1 || level > 100) { MessageBoxW(gMain, L"Level must be between 1 and 100.", L"Invalid level", MB_ICONWARNING); return; }
    std::vector<double> values(std::size(kStats), 0.0);
    for (const auto& p : gStatEdits) {
        double v = 0;
        if (!parseNumber(textOf(p.second), v)) {
            MessageBoxW(gMain, (std::wstring(L"Please enter a valid non-negative number for ") + kStats[static_cast<size_t>(p.first)].label + L".").c_str(),
                        L"Invalid stat", MB_ICONWARNING);
            return;
        }
        values[static_cast<size_t>(p.first)] = v;
    }
    if (values[0] <= 0 || values[2] <= 0) {
        MessageBoxW(gMain, L"Health and Speed must both be greater than zero.", L"Missing stats", MB_ICONWARNING); return;
    }
    double basicMult = 0;
    if (!parseNumber(textOf(gBasicMult), basicMult) || basicMult <= 0) {
        MessageBoxW(gMain, L"Basic ability multiplier must be greater than zero.", L"Invalid ability", MB_ICONWARNING); return;
    }
    const std::string basicName = narrow(trimW(textOf(gBasicName))).empty() ? "Basic Attack" : narrow(trimW(textOf(gBasicName)));
    const std::string specialName = narrow(trimW(textOf(gSpecialName)));
    double specialMult = 0, chancePct = 100, durationD = 2, cooldownD = 3;
    if (!specialName.empty()) {
        if (!parseNumber(textOf(gSpecialMult), specialMult) || specialMult <= 0 ||
            !parseNumber(textOf(gSpecialChance), chancePct) || chancePct > 100 ||
            !parseNumber(textOf(gSpecialDuration), durationD) || durationD > 99 ||
            !parseNumber(textOf(gSpecialCd), cooldownD) || cooldownD > 99) {
            MessageBoxW(gMain, L"Check the special ability multiplier, cooldown, status chance and duration.", L"Invalid special ability", MB_ICONWARNING); return;
        }
    }
    const fs::path folder = gDataDir / "user";
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) { MessageBoxW(gMain, L"Could not create the data/user folder.", L"Save failed", MB_ICONERROR); return; }
    const fs::path file = folder / (id + ".json");
    if (fs::exists(file, ec)) {
        MessageBoxW(gMain, L"A user-data file with this unit ID already exists. Change the unit name or edit the existing file.", L"Duplicate unit", MB_ICONWARNING);
        return;
    }
    std::ostringstream j;
    j << "{\n  \"units\": [\n    {\n"
      << "      \"id\": \"" << jsonEscape(id) << "\",\n"
      << "      \"name\": \"" << jsonEscape(name) << "\",\n"
      << "      \"tags\": [";
    const auto tags = splitTags(narrow(textOf(gTags)));
    for (size_t i = 0; i < tags.size(); ++i) j << (i ? ", " : "") << "\"" << jsonEscape(tags[i]) << "\"";
    j << "],\n      \"level\": " << level << ",\n      \"stats\": {\n";
    for (size_t i = 0; i < std::size(kStats); ++i)
        j << "        \"" << kStats[i].key << "\": " << number(values[i]) << (i + 1 == std::size(kStats) ? "\n" : ",\n");
    j << "      },\n      \"abilities\": [\n"
      << "        {\"id\": \"" << jsonEscape(id + "_basic") << "\", \"name\": \"" << jsonEscape(basicName)
      << "\", \"kind\": \"basic\", \"pick\": \"enemy\", \"description\": \"Basic attack\", "
      << "\"effects\": [{\"type\": \"damage\", \"damage_type\": \"" << damageType(gBasicType)
      << "\", \"multiplier\": " << number(basicMult) << "}]}\n";
    if (!specialName.empty()) {
        j << "        ,{\"id\": \"" << jsonEscape(id + "_special") << "\", \"name\": \"" << jsonEscape(specialName)
          << "\", \"kind\": \"special\", \"pick\": \"enemy\", \"cooldown\": " << static_cast<int>(cooldownD)
          << ", \"description\": \"User-defined special ability\", \"effects\": [";
        bool comma = false;
        const std::string statusId = selectedStatusId();
        const int statusTarget = static_cast<int>(SendMessageW(gSpecialStatusTarget, CB_GETCURSEL, 0, 0));
        if (!statusId.empty() && statusTarget == 0) {
            j << "{\"type\":\"damage\",\"damage_type\":\"" << damageType(gSpecialType) << "\",\"multiplier\":" << number(specialMult)
              << ",\"on_hit\":[{\"type\":\"apply_status\",\"status\":\"" << jsonEscape(statusId)
              << "\",\"chance\":" << number(chancePct / 100.0) << ",\"duration\":" << static_cast<int>(durationD) << "}]}";
            comma = true;
        } else {
            j << "{\"type\":\"damage\",\"damage_type\":\"" << damageType(gSpecialType) << "\",\"multiplier\":" << number(specialMult) << "}";
            comma = true;
            if (!statusId.empty()) {
                const char* target = statusTarget == 1 ? "self" : statusTarget == 2 ? "all_allies" : "all_enemies";
                j << ",{\"type\":\"apply_status\",\"status\":\"" << jsonEscape(statusId) << "\",\"target\":\"" << target
                  << "\",\"chance\":" << number(chancePct / 100.0) << ",\"duration\":" << static_cast<int>(durationD) << "}";
            }
        }
        if (SendMessageW(gSpecialBonus, BM_GETCHECK, 0, 0) == BST_CHECKED)
            j << (comma ? "," : "") << "{\"type\":\"bonus_move\",\"target\":\"self\"}";
        j << "]}\n";
    }
    j << "      ]\n    }\n  ]\n}\n";
    {
        std::ofstream out(file, std::ios::binary);
        if (!out) { MessageBoxW(gMain, L"Could not write the unit file.", L"Save failed", MB_ICONERROR); return; }
        out << j.str();
        if (!out) { out.close(); fs::remove(file, ec); MessageBoxW(gMain, L"Writing the unit file failed.", L"Save failed", MB_ICONERROR); return; }
    }
    try {
        refreshContent(); // Loader validates JSON, required basic ability, status IDs and all cross-references.
    } catch (const std::exception& e) {
        fs::remove(file, ec);
        MessageBoxW(gMain, (L"Unit was not saved because the game-data validator rejected it:\n\n" + widen(e.what())).c_str(),
                    L"Validation failed", MB_ICONERROR);
        return;
    }
    updateHome();
    const std::wstring msg = L"Saved and validated " + widen(name) + L".\n\nFile: " + widen(file.wstring()) +
        L"\n\nThe unit is now available to custom teams in the simulator.";
    MessageBoxW(gMain, msg.c_str(), L"Unit added", MB_ICONINFORMATION);
    clearUnitForm();
}
std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> v;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) { item = trim(item); if (!item.empty()) v.push_back(item); }
    return v;
}
bool resolveTeam(const Content& c, const std::string& spec, std::vector<std::string>& out, std::string& err) {
    if (const TeamDef* t = c.team(spec)) { out = t->units; return true; }
    out = splitCsv(spec);
    if (out.empty()) { err = "Enter a team ID or one or more comma-separated unit IDs."; return false; }
    if (out.size() > 5) { err = "A team can contain no more than five units."; return false; }
    for (const auto& id : out) if (!c.units.count(id)) { err = "Unknown unit ID: " + id; return false; }
    return true;
}
struct SimResult { std::string output; bool ok = false; };
void runSimulation() {
    if (gBusy) return;
    const std::string teamA = narrow(trimW(textOf(gSimA))), teamB = narrow(trimW(textOf(gSimB)));
    int runs = 100;
    try { runs = std::stoi(narrow(trimW(textOf(gSimRuns)))); } catch (...) { runs = 0; }
    if (runs < 1 || runs > 100000) { MessageBoxW(gMain, L"Battles must be between 1 and 100,000.", L"Invalid count", MB_ICONWARNING); return; }
    const std::string seedText = narrow(trimW(textOf(gSimSeed)));
    std::uint64_t seed = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) % 1000000000ULL;
    if (!seedText.empty()) {
        try { seed = std::stoull(seedText); } catch (...) { MessageBoxW(gMain, L"Seed must be a whole number.", L"Invalid seed", MB_ICONWARNING); return; }
    }
    gBusy = true;
    EnableWindow(gSimButton, FALSE);
    setText(gSimOutput, L"Simulation running...");
    fs::path data = gDataDir;
    gWorker = std::thread([teamA, teamB, runs, seed, data]() {
        auto result = std::make_unique<SimResult>();
        try {
            Content c = loadContent((data / "content.json").string());
            Rules rules;
            if (fs::exists(data / "rules.json")) rules = Rules::fromJson(json::parseFile((data / "rules.json").string()));
            std::vector<std::string> a, b; std::string err;
            if (!resolveTeam(c, teamA, a, err) || !resolveTeam(c, teamB, b, err)) throw std::runtime_error(err);
            int winsA = 0, winsB = 0, draws = 0;
            double turns = 0;
            std::string firstLog;
            AiController ai;
            for (int i = 0; i < runs; ++i) {
                Battle battle(c, rules, seed + static_cast<std::uint64_t>(i));
                battle.addTeam(0, a); battle.addTeam(1, b); battle.start();
                battle.logging = (i == 0);
                battle.run(ai, ai);
                int w = battle.winner();
                if (w == 0) ++winsA; else if (w == 1) ++winsB; else ++draws;
                turns += battle.turnsTaken;
                if (i == 0) {
                    std::ostringstream trace;
                    for (const auto& line : battle.log) trace << line << "\r\n";
                    firstLog = trace.str();
                }
            }
            std::ostringstream out;
            out << "Simulation complete\r\n"
                << "Team A: " << teamA << "\r\nTeam B: " << teamB
                << "\r\nBattles: " << runs << "\r\nSeed: " << seed << "\r\n\r\n"
                << "TEAM A WINS: " << winsA << " (" << std::fixed << std::setprecision(1) << (100.0 * winsA / runs) << "%)\r\n"
                << "TEAM B WINS: " << winsB << " (" << (100.0 * winsB / runs) << "%)\r\n"
                << "DRAWS: " << draws << " (" << (100.0 * draws / runs) << "%)\r\n"
                << "AVERAGE TURNS: " << std::setprecision(2) << (turns / runs) << "\r\n\r\n"
                << "FIRST BATTLE TRACE\r\n------------------\r\n" << firstLog;
            result->output = out.str(); result->ok = true;
        } catch (const std::exception& e) { result->output = std::string("Simulation failed:\r\n") + e.what(); }
        PostMessageW(gMain, WM_SIM_DONE, 0, reinterpret_cast<LPARAM>(result.release()));
    });
}
void validateData() {
    try {
        refreshContent();
        updateHome();
        const std::string msg = "Data validation passed.\n\nLoaded " + std::to_string(gContent->units.size()) +
            " units, " + std::to_string(gContent->teams.size()) + " teams and " + std::to_string(gContent->statuses.size()) +
            " statuses. The loader checked the unit files, ability definitions, status IDs and cross-references.";
        MessageBoxW(gMain, widen(msg).c_str(), L"Validation passed", MB_ICONINFORMATION);
    } catch (const std::exception& e) {
        MessageBoxW(gMain, widen(e.what()).c_str(), L"Data validation failed", MB_ICONERROR);
    }
}
void createNav() {
    HWND h = CreateWindowExW(0, L"BUTTON", L"Overview", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 15, 130, 36, gMain,
                             reinterpret_cast<HMENU>(ID_NAV_HOME), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
    const struct { const wchar_t* text; int id; int x; } tabs[] = {
        {L"Add unit", ID_NAV_UNIT, 160}, {L"Status library", ID_NAV_STATUS, 300}, {L"Battle simulator", ID_NAV_SIM, 440}
    };
    for (const auto& t : tabs) {
        HWND b = CreateWindowExW(0, L"BUTTON", t.text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, t.x, 15, 130, 36, gMain,
                                 reinterpret_cast<HMENU>(t.id), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
    }
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        gMain = hwnd;
        gFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        createNav();
        showPage(0);
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wp), code = HIWORD(wp);
        if (id == ID_NAV_HOME && code == BN_CLICKED) { showPage(0); return 0; }
        if (id == ID_NAV_UNIT && code == BN_CLICKED) { showPage(1); return 0; }
        if (id == ID_NAV_STATUS && code == BN_CLICKED) { showPage(2); return 0; }
        if (id == ID_NAV_SIM && code == BN_CLICKED) { showPage(3); return 0; }
        if (id == ID_NAME && code == EN_CHANGE) { updateUnitId(); return 0; }
        if (id == ID_SAVE_UNIT && code == BN_CLICKED) { saveUnit(); return 0; }
        if (id == ID_CLEAR_UNIT && code == BN_CLICKED) { clearUnitForm(); return 0; }
        if (id == ID_VALIDATE && code == BN_CLICKED) { validateData(); return 0; }
        if (id == ID_STATUS_LIST && code == LBN_SELCHANGE) { updateStatusDetail(); return 0; }
        if ((id == ID_STATUS_FILTER && code == CBN_SELCHANGE) || (id == ID_STATUS_SEARCH && code == EN_CHANGE)) {
            populateStatusList(); return 0;
        }
        if (id == ID_SIM_RUN && code == BN_CLICKED) { runSimulation(); return 0; }
        break;
    }
    case WM_SIM_DONE: {
        std::unique_ptr<SimResult> result(reinterpret_cast<SimResult*>(lp));
        if (gWorker.joinable()) gWorker.join();
        gBusy = false;
        if (gSimButton) EnableWindow(gSimButton, TRUE);
        if (gSimOutput && result) setText(gSimOutput, widen(result->output));
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* m = reinterpret_cast<MINMAXINFO*>(lp);
        m->ptMinTrackSize.x = 1180; m->ptMinTrackSize.y = 900;
        return 0;
    }
    case WM_DESTROY:
        if (gWorker.joinable()) gWorker.join();
        if (gFont) DeleteObject(gFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(nullptr, module, MAX_PATH);
    gExeDir = fs::path(module).parent_path();
    gDataDir = findDataFolder();
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return 1;
    gMain = CreateWindowExW(0, kClassName, L"GoHClone Desktop", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1220, 965, nullptr, nullptr, instance, nullptr);
    if (!gMain) return 1;
    ShowWindow(gMain, show);
    UpdateWindow(gMain);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
