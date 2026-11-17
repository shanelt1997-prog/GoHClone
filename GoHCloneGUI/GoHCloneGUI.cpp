#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <objbase.h>
#include <winhttp.h>

#include "Ai.h"
#include "Battle.h"
#include "Defs.h"
#include "Json.h"
#include "Rules.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cctype>
#include <cwctype>
#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <atomic>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Winhttp.lib")

namespace fs = std::filesystem;

namespace {
constexpr wchar_t kWindowClass[] = L"GoHCloneDesktopV2";
enum {
    M_DATA_COMLINK = 999,
    M_FILE_SELECT = 1000, M_FILE_RELOAD, M_FILE_EXIT,
    M_ROSTER_HOME, M_ROSTER_ADD, M_ROSTER_STATUS,
    M_BATTLE_SIM, M_BATTLE_VALIDATE,
    M_HELP_ABOUT,
    ID_NAME = 2000, ID_LEVEL, ID_TAGS, ID_UNIT_ID,
    ID_SAVE_UNIT, ID_CLEAR_UNIT,
    ID_ABILITY_NAME, ID_ABILITY_KIND, ID_ABILITY_PICK, ID_ABILITY_CD, ID_ABILITY_CHARGE, ID_ABILITY_DESC,
    ID_ADD_ABILITY, ID_REMOVE_ABILITY, ID_ABILITY_LIST,
    ID_EFFECT_TYPE, ID_EFFECT_TARGET, ID_EFFECT_DAMAGE, ID_EFFECT_STATUS, ID_EFFECT_VALUE, ID_EFFECT_AUX,
    ID_EFFECT_CHANCE, ID_EFFECT_DURATION, ID_EFFECT_AMOUNT, ID_EFFECT_DISPEL, ID_EFFECT_ABILITY,
    ID_ADD_EFFECT, ID_REMOVE_EFFECT, ID_EFFECT_LIST,
    ID_TRIGGER_EVENT, ID_TRIGGER_SCOPE,
    ID_STATUS_FILTER, ID_STATUS_SEARCH, ID_STATUS_LIST, ID_STATUS_DETAIL,
    ID_SIM_A, ID_SIM_B, ID_SIM_RUNS, ID_SIM_SEED, ID_SIM_RUN, ID_SIM_OUTPUT,
    ID_HOME_ADD, ID_HOME_STATUS, ID_HOME_SIM, ID_HOME_UNITS, ID_HOME_COUNT
};

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
    {L"Armor penetration", "armor_pen"}, {L"Resistance penetration", "resistance_pen"},
    {L"Defense penetration (%)", "defense_pen_pct"},
    {L"Counter chance (%)", "counter_chance_pct"},
    {L"Damage dealt multiplier (x)", "damage_dealt_mult"},
    {L"Damage taken multiplier (x)", "damage_taken_mult"},
    {L"Out-of-turn damage multiplier (x)", "out_of_turn_damage_mult"}
};

HWND gMain = nullptr;
HFONT gFont = nullptr;
HBRUSH gBackgroundBrush=nullptr, gPanelBrush=nullptr, gEditBrush=nullptr, gMenuBrush=nullptr;
constexpr COLORREF kSpaceBlack=RGB(10,16,25);
constexpr COLORREF kPanel=RGB(20,32,46);
constexpr COLORREF kInput=RGB(13,24,36);
constexpr COLORREF kGold=RGB(232,196,104);
constexpr COLORREF kText=RGB(222,230,238);
constexpr COLORREF kMuted=RGB(143,164,183);
constexpr COLORREF kSelected=RGB(34,70,91);
fs::path gExeDir, gDataDir;
std::unique_ptr<Content> gContent;
std::vector<HWND> gPage;
std::vector<std::pair<int, HWND>> gStatEdits;
HWND gName=nullptr, gUnitId=nullptr, gLevel=nullptr, gTags=nullptr;
HWND gAbilityName=nullptr, gAbilityKind=nullptr, gAbilityPick=nullptr, gAbilityCd=nullptr, gAbilityCharge=nullptr, gAbilityDesc=nullptr;
HWND gEffectType=nullptr, gEffectTarget=nullptr, gEffectDamage=nullptr, gEffectStatus=nullptr, gEffectValue=nullptr, gEffectAux=nullptr;
HWND gEffectChance=nullptr, gEffectDuration=nullptr, gEffectAmount=nullptr, gEffectDispel=nullptr, gEffectAbility=nullptr;
HWND gTriggerEvent=nullptr, gTriggerScope=nullptr;
HWND gAbilityList=nullptr, gEffectList=nullptr;
HWND gStatusFilter=nullptr, gStatusSearch=nullptr, gStatusList=nullptr, gStatusDetail=nullptr;
HWND gSimA=nullptr, gSimB=nullptr, gSimRuns=nullptr, gSimSeed=nullptr, gSimOutput=nullptr, gSimButton=nullptr;
HWND gHomeUnits=nullptr, gHomeCount=nullptr, gHomeData=nullptr;
std::vector<std::string> gStatusIds, gFilteredStatuses;
std::vector<std::string> gEffectJson, gAbilityJson, gAbilityLabels;
std::thread gWorker;
bool gBusy=false;
struct ComlinkResult { std::string text; bool ok=false; };

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring w(static_cast<size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);
    return w;
}
std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);
    std::string s(static_cast<size_t>(n),'\0');
    WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),s.data(),n,nullptr,nullptr);
    return s;
}
std::wstring getText(HWND h) {
    int n=GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n)+1,L'\0');
    GetWindowTextW(h,s.data(),n+1);
    s.resize(static_cast<size_t>(n));
    return s;
}
void setText(HWND h,const std::wstring& s) { if(h) SetWindowTextW(h,s.c_str()); }
std::wstring trimW(std::wstring s) {
    while(!s.empty() && iswspace(s.front())) s.erase(s.begin());
    while(!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}
std::string trim(std::string s) {
    auto ws=[](unsigned char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
    while(!s.empty()&&ws(static_cast<unsigned char>(s.front())))s.erase(s.begin());
    while(!s.empty()&&ws(static_cast<unsigned char>(s.back())))s.pop_back();
    return s;
}
std::string escapeJson(const std::string& s) {
    std::string o;
    for(unsigned char c:s) {
        if(c=='"') o+="\\\"";
        else if(c=='\\') o+="\\\\";
        else if(c=='\n') o+="\\n";
        else if(c=='\r') o+="\\r";
        else if(c=='\t') o+="\\t";
        else if(c<0x20) { char b[8]; std::snprintf(b,sizeof b,"\\u%04x",c); o+=b; }
        else o+=static_cast<char>(c);
    }
    return o;
}
std::string fmt(double v) { std::ostringstream s; s<<std::setprecision(12)<<v; return s.str(); }
bool numberFrom(HWND h,double& v) {
    auto s=trimW(getText(h));
    if(s.empty()){v=0;return true;}
    wchar_t* end=nullptr;
    v=wcstod(s.c_str(),&end);
    return end!=s.c_str()&&*end==L'\0'&&std::isfinite(v)&&v>=0;
}
bool signedNumberFrom(HWND h,double& v) {
    auto s=trimW(getText(h));
    if(s.empty()){v=0;return true;}
    wchar_t* end=nullptr;
    v=wcstod(s.c_str(),&end);
    return end!=s.c_str()&&*end==L'\0'&&std::isfinite(v);
}
bool durationFrom(HWND h,int& v) {
    const auto s=trimW(getText(h));
    if(s.empty()){v=-999;return true;} // use the status definition's default duration
    if(s==L"-1"){v=-1;return true;} // permanent
    wchar_t* end=nullptr;
    long n=std::wcstol(s.c_str(),&end,10);
    if(end==s.c_str()||*end!=L'\0'||n<0||n>99)return false;
    v=static_cast<int>(n);return true;
}
std::string slug(const std::wstring& s) {
    std::string o; bool sep=false;
    for(wchar_t c:s) {
        if((c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')) {
            o+=static_cast<char>(c>=L'A'&&c<=L'Z'?c-L'A'+L'a':c); sep=false;
        } else if(!o.empty()&&!sep){o+='_';sep=true;}
    }
    while(!o.empty()&&o.back()=='_')o.pop_back();
    return o;
}
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> v; std::stringstream ss(s); std::string x;
    while(std::getline(ss,x,',')){x=trim(x);if(!x.empty())v.push_back(x);}
    return v;
}
std::string comboText(HWND h) {
    int n=static_cast<int>(SendMessageW(h,CB_GETCURSEL,0,0));
    if(n<0)return {};
    int len=static_cast<int>(SendMessageW(h,CB_GETLBTEXTLEN,n,0));
    if(len<0)return {};
    std::wstring w(static_cast<size_t>(len)+1,L'\0');
    SendMessageW(h,CB_GETLBTEXT,n,reinterpret_cast<LPARAM>(w.data()));
    w.resize(static_cast<size_t>(len));
    return toUtf8(w);
}
void pageAdd(HWND h) { gPage.push_back(h); if(gFont)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(gFont),TRUE); }
HWND makeCtl(DWORD ex,const wchar_t* cls,const std::wstring& title,DWORD style,int id,int x,int y,int w,int h) {
    HWND c=CreateWindowExW(ex,cls,title.c_str(),WS_CHILD|WS_VISIBLE|style,x,y,w,h,gMain,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    pageAdd(c); return c;
}
HWND lab(const std::wstring& s,int x,int y,int w=140,int h=22) {return makeCtl(0,L"STATIC",s,SS_LEFT,0,x,y,w,h);}
HWND edit(const std::wstring& s,int id,int x,int y,int w=160,int h=25,bool readOnly=false) {
    HWND c=makeCtl(WS_EX_CLIENTEDGE,L"EDIT",s,ES_AUTOHSCROLL|WS_TABSTOP,id,x,y,w,h);
    if(readOnly)SendMessageW(c,EM_SETREADONLY,TRUE,0);
    return c;
}
HWND btn(const std::wstring& s,int id,int x,int y,int w=130,int h=32) {return makeCtl(0,L"BUTTON",s,BS_PUSHBUTTON|WS_TABSTOP,id,x,y,w,h);}
HWND group(const std::wstring& s,int x,int y,int w,int h) {return makeCtl(0,L"BUTTON",s,BS_GROUPBOX,0,x,y,w,h);}
HWND combo(int id,int x,int y,int w=160) {return makeCtl(0,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,id,x,y,w,220);}
HWND listbox(int id,int x,int y,int w,int h) {return makeCtl(WS_EX_CLIENTEDGE,L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_TABSTOP,id,x,y,w,h);}
void addCombo(HWND h,const wchar_t* s){SendMessageW(h,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));}
void clearPage(){for(HWND h:gPage)if(IsWindow(h))DestroyWindow(h);gPage.clear();gStatEdits.clear();}

fs::path locateData() {
    std::error_code ec;
    fs::path p=gExeDir, found;
    for(int i=0;i<10&&!p.empty();++i){fs::path q=p/"data";if(fs::exists(q/"content.json",ec))found=q;p=p.parent_path();}
    p=fs::current_path(ec);
    for(int i=0;i<8&&!p.empty();++i){fs::path q=p/"data";if(fs::exists(q/"content.json",ec))found=q;p=p.parent_path();}
    return found;
}
void reloadData() {
    if(gDataDir.empty()) throw std::runtime_error("Data folder not found. Use File > Choose data folder and select the folder containing content.json.");
    gContent=std::make_unique<Content>(loadContent((gDataDir/"content.json").string()));
}
void updateNameId(){if(gName&&gUnitId)setText(gUnitId,fromUtf8(slug(getText(gName))));}
void resetForm() {
    setText(gName,L"");setText(gUnitId,L"");setText(gLevel,L"85");setText(gTags,L"");
    for(auto& p:gStatEdits)setText(p.second,L"");
    gAbilityJson.clear();gAbilityLabels.clear();gEffectJson.clear();
    if(gAbilityList)SendMessageW(gAbilityList,LB_RESETCONTENT,0,0);
    if(gEffectList)SendMessageW(gEffectList,LB_RESETCONTENT,0,0);
    setText(gAbilityName,L"");setText(gAbilityCd,L"3");setText(gAbilityCharge,L"100");setText(gAbilityDesc,L"");
    setText(gEffectValue,L"1.0");setText(gEffectAux,L"0");setText(gEffectChance,L"100");
    setText(gEffectDuration,L"");setText(gEffectAmount,L"0");setText(gEffectAbility,L"*");
    SendMessageW(gAbilityKind,CB_SETCURSEL,0,0);SendMessageW(gAbilityPick,CB_SETCURSEL,0,0);
    SendMessageW(gEffectType,CB_SETCURSEL,0,0);SendMessageW(gEffectTarget,CB_SETCURSEL,0,0);
    SendMessageW(gEffectDamage,CB_SETCURSEL,0,0);SendMessageW(gEffectStatus,CB_SETCURSEL,0,0);
    SendMessageW(gEffectDispel,CB_SETCURSEL,0,0);SendMessageW(gTriggerEvent,CB_SETCURSEL,0,0);
    SendMessageW(gTriggerScope,CB_SETCURSEL,0,0);
}
std::string statusIdAtCombo(HWND h) {
    int n=static_cast<int>(SendMessageW(h,CB_GETCURSEL,0,0));
    if(n<0)return {};
    LRESULT v=SendMessageW(h,CB_GETITEMDATA,n,0);
    if(v==CB_ERR)return {};
    size_t i=static_cast<size_t>(v);
    return i<gStatusIds.size()?gStatusIds[i]:std::string();
}
void fillStatusCombo(HWND h) {
    gStatusIds.clear();
    addCombo(h,L"(No status)");
    SendMessageW(h,CB_SETITEMDATA,0,static_cast<LPARAM>(-1));
    if(!gContent)return;
    for(size_t i=0;i<gContent->statusOrder.size();++i) {
        const auto& id=gContent->statusOrder[i];
        auto it=gContent->statuses.find(id);if(it==gContent->statuses.end())continue;
        std::wstring s=fromUtf8(it->second.name+" ["+id+"]");
        int n=static_cast<int>(SendMessageW(h,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s.c_str())));
        SendMessageW(h,CB_SETITEMDATA,n,static_cast<LPARAM>(gStatusIds.size()));
        gStatusIds.push_back(id);
    }
    SendMessageW(h,CB_SETCURSEL,0,0);
}
void updateHome() {
    if(!gHomeUnits||!gContent)return;
    setText(gHomeUnits,fromUtf8(std::to_string(gContent->units.size())+" units loaded  |  "+std::to_string(gContent->teams.size())+" demo teams"));
    size_t buffs=0,debuffs=0,neutral=0;
    for(const auto& p:gContent->statuses){if(p.second.kind==StatusKind::Buff)++buffs;else if(p.second.kind==StatusKind::Debuff)++debuffs;else ++neutral;}
    setText(gHomeCount,fromUtf8(std::to_string(buffs)+" buffs  |  "+std::to_string(debuffs)+" debuffs  |  "+std::to_string(neutral)+" neutral"));
    setText(gHomeData,fromUtf8(gDataDir.string()));
}

void refreshStatusList();
void updateStatusDetail();
void createHome();
void createUnitEditor();
void createStatuses();
void createSimulator();
void showPage(int page) {
    clearPage();
    try{reloadData();}catch(const std::exception& e){MessageBoxW(gMain,fromUtf8(e.what()).c_str(),L"Data loading",MB_ICONWARNING);}
    if(page==0)createHome();else if(page==1)createUnitEditor();else if(page==2)createStatuses();else createSimulator();
}
void createHome() {
    lab(L"GOHCLONE  //  TACTICAL HOLO-ARCHIVE",30,28,900,42);
    lab(L"UNIT INTELLIGENCE  |  COMBAT ANALYSIS  |  GALACTIC DATA SYSTEM",30,74,1100,25);
    group(L"HOLO-NET STATUS",25,125,1160,190);
    lab(L"Loaded roster",45,165,150);gHomeUnits=lab(L"",200,165,850,25);
    lab(L"Status definitions",45,210,150);gHomeCount=lab(L"",200,210,850,25);
    lab(L"Data folder",45,255,150);gHomeData=lab(L"",200,255,930,25);
    btn(L"Add a new unit",ID_HOME_ADD,30,355,180,42);
    btn(L"Browse buffs and debuffs",ID_HOME_STATUS,230,355,230,42);
    btn(L"Open battle simulator",ID_HOME_SIM,480,355,210,42);
    group(L"MISSION CONTROL",25,435,1160,180);
    lab(L"Use Roster > Add unit to create a complete multi-ability unit entry.",45,475,1080,25);
    lab(L"Use the status browser to search every status loaded from your JSON library.",45,515,1080,25);
    lab(L"Battle > Validate data checks all loaded unit, ability and status references.",45,555,1080,25);
    updateHome();
}
void createUnitEditor() {
    lab(L"Unit editor",25,20,420,35);
    lab(L"Enter stats as shown in-game. Percentages should be entered as numbers (for example, 105.2).",25,55,1120,24);
    group(L"UNIT DETAILS",20,90,1180,75);
    lab(L"Name",35,120,50);gName=edit(L"",ID_NAME,90,116,190);
    lab(L"Generated ID",300,120,100);gUnitId=edit(L"",ID_UNIT_ID,400,116,170,25,true);
    lab(L"Level",590,120,45);gLevel=edit(L"85",635,116,55);
    lab(L"Tags (comma separated)",715,120,155);gTags=edit(L"",ID_TAGS,870,116,285);
    group(L"UNIT STATS",20,175,1180,210);
    for(size_t i=0;i<std::size(kStats);++i){
        int col=static_cast<int>(i/6),row=static_cast<int>(i%6);
        int x=32+col*285,y=205+row*28;
        lab(kStats[i].label,x,y,160,22);
        HWND e=edit(L"",2100+static_cast<int>(i),x+160,y-3,90);
        gStatEdits.push_back({static_cast<int>(i),e});
    }
    group(L"ABILITIES (ADD AS MANY AS THE UNIT NEEDS)",20,395,1180,475);
    lab(L"Ability name",35,425,95);gAbilityName=edit(L"",ID_ABILITY_NAME,130,421,185);
    lab(L"Type",330,425,40);gAbilityKind=combo(ID_ABILITY_KIND,370,421,125);
    addCombo(gAbilityKind,L"Basic");addCombo(gAbilityKind,L"Special");addCombo(gAbilityKind,L"Ultimate");addCombo(gAbilityKind,L"Leader");addCombo(gAbilityKind,L"Unique");
    SendMessageW(gAbilityKind,CB_SETCURSEL,0,0);
    lab(L"Target to choose",510,425,100);gAbilityPick=combo(ID_ABILITY_PICK,610,421,130);
    addCombo(gAbilityPick,L"Enemy");addCombo(gAbilityPick,L"Ally");addCombo(gAbilityPick,L"Dead ally");addCombo(gAbilityPick,L"Self");addCombo(gAbilityPick,L"No target");
    SendMessageW(gAbilityPick,CB_SETCURSEL,0,0);
    lab(L"Cooldown",755,425,65);gAbilityCd=edit(L"3",ID_ABILITY_CD,820,421,55);
    lab(L"Ult charge",890,425,70);gAbilityCharge=edit(L"100",ID_ABILITY_CHARGE,960,421,55);
    lab(L"Description",35,458,85);gAbilityDesc=edit(L"",ID_ABILITY_DESC,120,454,1035);
    group(L"ABILITY EFFECTS - build the actions this ability performs",30,492,1155,210);
    lab(L"Effect",42,520,48);gEffectType=combo(ID_EFFECT_TYPE,90,516,160);
    const wchar_t* effects[]={L"Damage",L"Heal",L"Apply status",L"Turn meter change",L"Cooldown change",L"Ultimate charge",L"Dispel",L"Revive",L"Bonus turn",L"Assist",L"Counter",L"Use ability again",L"Instant defeat",L"Remove status"};
    for(auto s:effects)addCombo(gEffectType,s);SendMessageW(gEffectType,CB_SETCURSEL,0,0);
    lab(L"Target",265,520,45);gEffectTarget=combo(ID_EFFECT_TARGET,310,516,145);
    const wchar_t* targets[]={L"Selected enemy",L"Selected ally",L"Selected dead ally",L"All enemies",L"All allies",L"Other allies",L"Self",L"Random enemy",L"Random ally",L"Lowest ally",L"Lowest enemy",L"Highest enemy",L"Leader ally",L"Last hit target",L"Event subject",L"Event other"};
    for(auto s:targets)addCombo(gEffectTarget,s);SendMessageW(gEffectTarget,CB_SETCURSEL,0,0);
    lab(L"Damage type",470,520,85);gEffectDamage=combo(ID_EFFECT_DAMAGE,555,516,115);
    addCombo(gEffectDamage,L"Physical");addCombo(gEffectDamage,L"Special");addCombo(gEffectDamage,L"True");SendMessageW(gEffectDamage,CB_SETCURSEL,0,0);
    lab(L"Status",685,520,45);gEffectStatus=combo(ID_EFFECT_STATUS,730,516,180);fillStatusCombo(gEffectStatus);
    lab(L"Dispel",925,520,45);gEffectDispel=combo(ID_EFFECT_DISPEL,970,516,100);
    addCombo(gEffectDispel,L"Buff");addCombo(gEffectDispel,L"Debuff");addCombo(gEffectDispel,L"All");SendMessageW(gEffectDispel,CB_SETCURSEL,0,0);
    lab(L"Multiplier / value",42,558,115);gEffectValue=edit(L"1.0",ID_EFFECT_VALUE,157,554,80);
    lab(L"Extra value",250,558,80);gEffectAux=edit(L"0",ID_EFFECT_AUX,330,554,70);
    lab(L"Chance %",420,558,70);gEffectChance=edit(L"100",ID_EFFECT_CHANCE,490,554,65);
    lab(L"Duration",575,558,65);gEffectDuration=edit(L"",ID_EFFECT_DURATION,640,554,55);lab(L"(blank = status default; -1 = permanent)",700,558,210,22);
    lab(L"Amount / stacks",715,558,100);gEffectAmount=edit(L"0",ID_EFFECT_AMOUNT,815,554,65);
    lab(L"Ability ref",900,558,70);gEffectAbility=edit(L"*",ID_EFFECT_ABILITY,970,554,120);
    lab(L"Passive trigger (optional)",42,592,170);gTriggerEvent=combo(ID_TRIGGER_EVENT,212,588,185);
    const wchar_t* evs[]={L"No trigger",L"When this unit gets a kill",L"When this unit is damaged",L"When this unit crits",L"At start of turn",L"At end of turn",L"When an ally is defeated",L"When an ally uses an ability",L"When a status is added"};
    for(auto s:evs)addCombo(gTriggerEvent,s);SendMessageW(gTriggerEvent,CB_SETCURSEL,0,0);
    lab(L"Scope",420,592,50);gTriggerScope=combo(ID_TRIGGER_SCOPE,470,588,120);
    addCombo(gTriggerScope,L"This unit");addCombo(gTriggerScope,L"Ally");addCombo(gTriggerScope,L"Any ally");addCombo(gTriggerScope,L"Enemy");addCombo(gTriggerScope,L"Any");
    SendMessageW(gTriggerScope,CB_SETCURSEL,0,0);
    btn(L"Add effect to ability",ID_ADD_EFFECT,620,615,170,30);
    btn(L"Remove selected effect",ID_REMOVE_EFFECT,800,615,180,30);
    gEffectList=listbox(ID_EFFECT_LIST,42,650,1090,42);
    btn(L"Add ability to unit",ID_ADD_ABILITY,280,704,165,32);
    btn(L"Remove selected ability",ID_REMOVE_ABILITY,455,704,190,32);
    gAbilityList=listbox(ID_ABILITY_LIST,35,742,1090,65);
    btn(L"Save and validate unit",ID_SAVE_UNIT,820,818,180,38);
    btn(L"Clear form",ID_CLEAR_UNIT,1015,818,120,38);
    updateNameId();
}
void createStatuses() {
    lab(L"Buff and debuff library",25,25,500,35);
    lab(L"These entries are loaded directly from data/library and the other project status files.",25,62,1100,24);
    gStatusFilter=combo(ID_STATUS_FILTER,25,108,170);
    addCombo(gStatusFilter,L"All statuses");addCombo(gStatusFilter,L"Buffs only");addCombo(gStatusFilter,L"Debuffs only");addCombo(gStatusFilter,L"Neutral only");
    SendMessageW(gStatusFilter,CB_SETCURSEL,0,0);
    lab(L"Search name, ID or description",220,112,210);
    gStatusSearch=edit(L"",ID_STATUS_SEARCH,430,106,310);
    gStatusList=listbox(ID_STATUS_LIST,25,155,500,650);
    lab(L"DETAILS",565,155,200,22);
    gStatusDetail=makeCtl(WS_EX_CLIENTEDGE,L"EDIT",L"Select an entry to view its details.",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,ID_STATUS_DETAIL,565,185,600,350);
    refreshStatusList();
}
void createSimulator() {
    lab(L"Battle simulator",25,25,500,35);
    lab(L"Enter a saved team ID or comma-separated unit IDs. The first custom unit is the leader.",25,62,1120,24);
    lab(L"Team A",25,115,55);gSimA=edit(L"concord_strike",ID_SIM_A,80,111,240);
    lab(L"Team B",355,115,55);gSimB=edit(L"syndicate_raiders",ID_SIM_B,410,111,240);
    lab(L"Battles",685,115,55);gSimRuns=edit(L"100",ID_SIM_RUNS,740,111,70);
    lab(L"Seed (optional)",850,115,95);gSimSeed=edit(L"",ID_SIM_SEED,945,111,150);
    gSimButton=btn(L"Run simulation",ID_SIM_RUN,25,160,160,38);
    btn(L"Validate data",M_BATTLE_VALIDATE,205,160,145,38);
    lab(L"RESULTS AND FIRST BATTLE TRACE",25,225,400,22);
    gSimOutput=makeCtl(WS_EX_CLIENTEDGE,L"EDIT",L"Choose two teams and run a simulation.",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_HSCROLL,ID_SIM_OUTPUT,25,255,1140,545);
}
void refreshStatusList() {
    if(!gStatusList||!gContent)return;
    SendMessageW(gStatusList,LB_RESETCONTENT,0,0);gFilteredStatuses.clear();
    int filter=static_cast<int>(SendMessageW(gStatusFilter,CB_GETCURSEL,0,0));
    std::string q=toUtf8(getText(gStatusSearch));
    std::transform(q.begin(),q.end(),q.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    for(const auto& id:gContent->statusOrder){
        auto it=gContent->statuses.find(id);if(it==gContent->statuses.end())continue;
        const auto& s=it->second;
        if(filter==1&&s.kind!=StatusKind::Buff)continue;
        if(filter==2&&s.kind!=StatusKind::Debuff)continue;
        if(filter==3&&s.kind!=StatusKind::Neutral)continue;
        std::string hay=s.name+" "+s.id+" "+s.description;
        std::transform(hay.begin(),hay.end(),hay.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(!q.empty()&&hay.find(q)==std::string::npos)continue;
        std::string k=s.kind==StatusKind::Buff?"BUFF":s.kind==StatusKind::Debuff?"DEBUFF":"NEUTRAL";
        std::wstring row=fromUtf8(k+"  |  "+s.name+"  ["+s.id+"]");
        int ix=static_cast<int>(SendMessageW(gStatusList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str())));
        SendMessageW(gStatusList,LB_SETITEMDATA,ix,static_cast<LPARAM>(gFilteredStatuses.size()));
        gFilteredStatuses.push_back(id);
    }
    if(!gFilteredStatuses.empty())SendMessageW(gStatusList,LB_SETCURSEL,0,0);
    updateStatusDetail();
}
void updateStatusDetail() {
    if(!gStatusList||!gStatusDetail||!gContent)return;
    int n=static_cast<int>(SendMessageW(gStatusList,LB_GETCURSEL,0,0));
    if(n<0||static_cast<size_t>(n)>=gFilteredStatuses.size()){setText(gStatusDetail,L"Select an entry to view its details.");return;}
    const auto& s=gContent->status(gFilteredStatuses[static_cast<size_t>(n)]);
    std::ostringstream o;o<<s.name<<"\r\nID: "<<s.id<<"\r\nType: "<<(s.kind==StatusKind::Buff?"Buff":s.kind==StatusKind::Debuff?"Debuff":"Neutral")
      <<"\r\nDefault duration: "<<s.duration<<(s.duration==-1?" (permanent)":" turns")<<"\r\nMaximum stacks: "<<s.maxStacks
      <<"\r\nDispellable: "<<(s.dispellable?"Yes":"No")<<"\r\nFlags:";
    const std::pair<std::uint32_t,const char*> flagNames[]={{SF_Taunt,"Taunt"},{SF_Stealth,"Stealth"},{SF_Stun,"Stun"},{SF_AbilityBlock,"Ability Block"},
      {SF_NoAssist,"No Assist"},{SF_NoCounter,"No Counter"},{SF_NoTMGain,"No Turn Meter Gain"},{SF_HealingImmunity,"Healing Immunity"},
      {SF_DamageImmunity,"Damage Immunity"},{SF_CritImmunity,"Critical Immunity"},{SF_DebuffImmunity,"Debuff Immunity"},
      {SF_BuffImmunity,"Buff Immunity"},{SF_IgnoreProtection,"Ignore Protection"},{SF_NoEvade,"Cannot Evade"},{SF_Foresight,"Foresight"},
      {SF_Advantage,"Advantage"},{SF_Blind,"Blind"},{SF_Vulnerable,"Vulnerable"},{SF_SpeedZero,"Speed Zero"},{SF_NoRevive,"No Revive"},
      {SF_InstantDefeatImmune,"Instant Defeat Immunity"},{SF_ProtectionDisabled,"Protection Disabled"},{SF_DamageToOne,"Damage To One"},
      {SF_TMImmune,"Turn Meter Immunity"},{SF_NoBuffsOffTurn,"No Buffs Out Of Turn"},{SF_IgnoreTaunt,"Ignore Taunt"},
      {SF_Uncounterable,"Uncounterable"},{SF_RiposteIgnoreProt,"Riposte Ignores Protection"},{SF_NoProtectionRecovery,"No Protection Recovery"}};
    bool anyFlag=false;for(const auto& flag:flagNames)if((s.flags&flag.first)!=0){o<<" "<<flag.second;anyFlag=true;}if(!anyFlag)o<<" None";
    const char* mode=s.mode==AmountMode::Dot?"Damage over time":s.mode==AmountMode::Hot?"Healing over time":
      s.mode==AmountMode::ProtOverTime?"Protection over time":s.mode==AmountMode::BonusProtection?"Bonus protection":"None";
    o<<"\r\nMode: "<<mode<<"\r\n\r\nDescription: "<<s.description;
    if(!s.mods.empty()){o<<"\r\n\r\nStat modifiers:";for(const auto& m:s.mods)o<<"\r\n - "<<statName(m.stat)<<" ("<<(m.op==ModOp::Add?"add":"% of base")<<"): "<<m.value;}
    if(!s.blocks.empty()){o<<"\r\n\r\nBlocks:";for(const auto& x:s.blocks)o<<"\r\n - "<<x;}
    if(!s.excludes.empty()){o<<"\r\n\r\nExcluded by:";for(const auto& x:s.excludes)o<<"\r\n - "<<x;}
    if(!s.triggers.empty())o<<"\r\n\r\nPassive reactions: "<<s.triggers.size();
    setText(gStatusDetail,fromUtf8(o.str()));
}

std::string effectTypeKey() {
    static const char* a[]={"damage","heal","apply_status","modify_turn_meter","modify_cooldown","modify_ultimate","dispel","revive","bonus_move","assist","counter","use_ability","defeat","remove_status"};
    int n=static_cast<int>(SendMessageW(gEffectType,CB_GETCURSEL,0,0));return n>=0&&n<14?a[n]:"damage";
}
std::string effectTargetKey() {
    static const char* a[]={"selected","selected_ally","selected_dead_ally","all_enemies","all_allies","other_allies","self","random_enemy","random_ally","lowest_ally","lowest_enemy","highest_enemy","leader_ally","hit_target","event_subject","event_other"};
    int n=static_cast<int>(SendMessageW(gEffectTarget,CB_GETCURSEL,0,0));return n>=0&&n<16?a[n]:"selected";
}
std::string effectDamageKey() {
    int n=static_cast<int>(SendMessageW(gEffectDamage,CB_GETCURSEL,0,0));return n==1?"ability_power":n==2?"unmitigable":"attack_damage";
}
std::string abilityKindKey() {
    static const char* a[]={"basic","special","ultimate","leader","unique"};
    int n=static_cast<int>(SendMessageW(gAbilityKind,CB_GETCURSEL,0,0));return n>=0&&n<5?a[n]:"basic";
}
std::string pickKey() {
    const std::string kind=abilityKindKey();
    if(kind=="leader"||kind=="unique")return "none";
    static const char* a[]={"enemy","ally","dead_ally","self","none"};
    int n=static_cast<int>(SendMessageW(gAbilityPick,CB_GETCURSEL,0,0));return n>=0&&n<5?a[n]:"enemy";
}
std::string triggerEventKey() {
    static const char* a[]={"","kill","receive_damage","crit","start_of_turn","end_of_turn","death","used_ability","status_added"};
    int n=static_cast<int>(SendMessageW(gTriggerEvent,CB_GETCURSEL,0,0));return n>=0&&n<9?a[n]:"";
}
std::string triggerScopeKey() {
    static const char* a[]={"self","ally","any_ally","enemy","any"};
    int n=static_cast<int>(SendMessageW(gTriggerScope,CB_GETCURSEL,0,0));return n>=0&&n<5?a[n]:"self";
}
void addEffect() {
    double value=1,aux=0,chance=100,amount=0;
    int duration=-999;
    if(!signedNumberFrom(gEffectValue,value)||!signedNumberFrom(gEffectAux,aux)||!numberFrom(gEffectChance,chance)||chance>100||
       !durationFrom(gEffectDuration,duration)||!numberFrom(gEffectAmount,amount)) {
        MessageBoxW(gMain,L"Check multiplier, extra value, chance, duration and amount. Values must be valid non-negative numbers.",L"Invalid effect",MB_ICONWARNING);return;
    }
    std::string type=effectTypeKey(),target=effectTargetKey(),status=statusIdAtCombo(gEffectStatus);
    if((type=="damage"||type=="heal"||type=="assist"||type=="counter")&&value<0){
        MessageBoxW(gMain,L"Damage, healing and reaction multipliers cannot be negative.",L"Invalid multiplier",MB_ICONWARNING);return;
    }
    if(type=="heal"&&aux<0){MessageBoxW(gMain,L"Protection healing multiplier cannot be negative.",L"Invalid healing value",MB_ICONWARNING);return;}
    if(amount>1000){MessageBoxW(gMain,L"Amount or stack count is too large.",L"Invalid amount",MB_ICONWARNING);return;}
    if((type=="apply_status"||type=="remove_status"||type=="dispel"||type=="damage")&&std::floor(amount)!=amount){
        MessageBoxW(gMain,L"Stacks, dispel count and hit count must be whole numbers.",L"Invalid count",MB_ICONWARNING);return;
    }
    if(type=="revive"&&amount>1){MessageBoxW(gMain,L"Revive health fraction must be between 0 and 1 (for example, 0.3 for 30%).",L"Invalid revive amount",MB_ICONWARNING);return;}
    if(type=="use_ability"){
        const std::string ref=trim(toUtf8(getText(gEffectAbility)));
        if(ref.empty()||ref=="*"){MessageBoxW(gMain,L"Enter the ability ID to use, or type basic.",L"Ability reference required",MB_ICONWARNING);return;}
    }
    std::ostringstream j;j<<"{\"type\":\""<<type<<"\",\"target\":\""<<target<<"\",\"chance\":"<<fmt(chance/100.0);
    if(type=="damage"){j<<",\"damage_type\":\""<<effectDamageKey()<<"\",\"multiplier\":"<<fmt(value);if(amount>0)j<<",\"hits\":"<<static_cast<int>(amount);}
    else if(type=="heal")j<<",\"multiplier\":"<<fmt(value)<<",\"protection_multiplier\":"<<fmt(aux);
    else if(type=="apply_status"){
        if(status.empty()){MessageBoxW(gMain,L"Choose a status for this effect.",L"Status required",MB_ICONWARNING);return;}
        j<<",\"status\":\""<<escapeJson(status)<<"\",\"duration\":"<<duration<<",\"stacks\":"<<std::max(1,static_cast<int>(amount));
        if(aux!=0)j<<",\"amount\":"<<fmt(aux);
    } else if(type=="remove_status"){
        if(status.empty()){MessageBoxW(gMain,L"Choose a status to remove.",L"Status required",MB_ICONWARNING);return;}
        j<<",\"status\":\""<<escapeJson(status)<<"\",\"count\":"<<static_cast<int>(amount);
    } else if(type=="revive")j<<",\"amount\":"<<fmt(amount);
    else if(type=="modify_turn_meter"||type=="modify_ultimate")j<<",\"amount\":"<<fmt(value);
    else if(type=="modify_cooldown") j << ",\"amount\":" << fmt(value) << ",\"ability\":\"" << escapeJson(toUtf8(getText(gEffectAbility))) << "\"";
    else if(type=="dispel"){
        std::string dispel=comboText(gEffectDispel);std::transform(dispel.begin(),dispel.end(),dispel.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        j<<",\"dispel\":\""<<dispel<<"\",\"count\":"<<static_cast<int>(amount);
    } else if(type=="assist"||type=="counter")j<<",\"multiplier\":"<<fmt(value);
    else if(type=="use_ability")j<<",\"ability\":\""<<escapeJson(toUtf8(getText(gEffectAbility)))<<"\",\"out_of_turn\":true,\"multiplier\":"<<fmt(value);
    j<<"}";
    try{json::parse(j.str());}catch(const std::exception& e){MessageBoxW(gMain,fromUtf8(e.what()).c_str(),L"Effect JSON error",MB_ICONERROR);return;}
    gEffectJson.push_back(j.str());
    std::string desc=type+" -> "+target;
    if(!status.empty()&&(type=="apply_status"||type=="remove_status"))desc+=" ("+status+")";
    int n=static_cast<int>(SendMessageW(gEffectList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(fromUtf8(desc).c_str())));
    SendMessageW(gEffectList,LB_SETCURSEL,n,0);
}
void removeEffect() {
    int n=static_cast<int>(SendMessageW(gEffectList,LB_GETCURSEL,0,0));
    if(n<0||static_cast<size_t>(n)>=gEffectJson.size())return;
    gEffectJson.erase(gEffectJson.begin()+n);SendMessageW(gEffectList,LB_DELETESTRING,n,0);
}
void addAbility() {
    std::string name=toUtf8(trimW(getText(gAbilityName))),id=slug(getText(gAbilityName)),kind=abilityKindKey();
    if(name.empty()||id.empty()){MessageBoxW(gMain,L"Enter an ability name first.",L"Ability name",MB_ICONWARNING);return;}
    if(kind=="unique"&&triggerEventKey().empty()){
        MessageBoxW(gMain,L"Choose a passive trigger event for a Unique ability.",L"Trigger required",MB_ICONWARNING);return;
    }
    if(kind=="unique"&&gEffectJson.empty()){
        MessageBoxW(gMain,L"Add at least one effect to the Unique ability's trigger.",L"No trigger effects",MB_ICONWARNING);return;
    }
    if((kind=="basic"||kind=="special"||kind=="ultimate")&&gEffectJson.empty()){
        MessageBoxW(gMain,L"Add at least one effect to this active ability.",L"No effects",MB_ICONWARNING);return;
    }
    for(const auto& a:gAbilityJson)if(a.find("\"id\":\""+id+"\"")!=std::string::npos){MessageBoxW(gMain,L"That ability name creates a duplicate ID. Use a different name.",L"Duplicate ability",MB_ICONWARNING);return;}
    double cd=3,charge=100;
    if(!numberFrom(gAbilityCd,cd)||cd>99||!numberFrom(gAbilityCharge,charge)||charge>1000){MessageBoxW(gMain,L"Cooldown must be 0-99 and ultimate charge must be a valid number.",L"Ability settings",MB_ICONWARNING);return;}
    std::ostringstream j;j<<"{\"id\":\""<<id<<"\",\"name\":\""<<escapeJson(name)<<"\",\"kind\":\""<<kind<<"\",\"pick\":\""<<pickKey()<<"\",\"description\":\""<<escapeJson(toUtf8(getText(gAbilityDesc)))<<"\"";
    if(kind=="special")j<<",\"cooldown\":"<<static_cast<int>(cd);
    if(kind=="ultimate")j<<",\"ultimate_charge\":"<<static_cast<int>(charge);
    if(kind=="unique"&&!triggerEventKey().empty()){
        j<<",\"triggers\":[{\"on\":\""<<triggerEventKey()<<"\",\"scope\":\""<<triggerScopeKey()<<"\",\"effects\":[";
        for(size_t i=0;i<gEffectJson.size();++i)j<<(i?",":"")<<gEffectJson[i];
        j<<"]}]";
    } else if(kind=="leader") {
        const std::string status=statusIdAtCombo(gEffectStatus);
        if(status.empty()){MessageBoxW(gMain,L"Select the status this Leader ability grants to all allies in the Status field.",L"Leader aura",MB_ICONWARNING);return;}
        j<<",\"auras\":[{\"target\":\"all_allies\",\"status\":\""<<escapeJson(status)<<"\",\"duration\":-1}]";
    } else {
        j<<",\"effects\":[";
        for(size_t i=0;i<gEffectJson.size();++i)j<<(i?",":"")<<gEffectJson[i];
        j<<"]";
    }
    j<<"}";
    try{json::parse(j.str());}catch(const std::exception& e){MessageBoxW(gMain,fromUtf8(e.what()).c_str(),L"Ability JSON error",MB_ICONERROR);return;}
    gAbilityJson.push_back(j.str());gAbilityLabels.push_back(kind+"  |  "+name);
    int n=static_cast<int>(SendMessageW(gAbilityList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(fromUtf8(gAbilityLabels.back()).c_str())));
    SendMessageW(gAbilityList,LB_SETCURSEL,n,0);
    gEffectJson.clear();SendMessageW(gEffectList,LB_RESETCONTENT,0,0);
    setText(gAbilityName,L"");setText(gAbilityDesc,L"");
}
void removeAbility() {
    int n=static_cast<int>(SendMessageW(gAbilityList,LB_GETCURSEL,0,0));
    if(n<0||static_cast<size_t>(n)>=gAbilityJson.size())return;
    gAbilityJson.erase(gAbilityJson.begin()+n);gAbilityLabels.erase(gAbilityLabels.begin()+n);
    SendMessageW(gAbilityList,LB_DELETESTRING,n,0);
}
void saveUnit() {
    std::string name=toUtf8(trimW(getText(gName))),id=slug(getText(gName));
    if(name.empty()){MessageBoxW(gMain,L"Enter a unit name.",L"Unit name",MB_ICONWARNING);return;}
    int level=85;try{level=std::stoi(toUtf8(trimW(getText(gLevel))));}catch(...){level=0;}
    if(level<1||level>100){MessageBoxW(gMain,L"Level must be a whole number between 1 and 100.",L"Invalid level",MB_ICONWARNING);return;}
    std::vector<double> statValues(std::size(kStats),0);
    std::vector<bool> statProvided(std::size(kStats),false);
    for(auto p:gStatEdits){
        const auto raw=trimW(getText(p.second));
        if(raw.empty())continue; // leave omitted values at the engine's normal defaults
        double v=0;
        if(!numberFrom(p.second,v)){MessageBoxW(gMain,fromUtf8(std::string("Invalid value for ")+kStats[static_cast<size_t>(p.first)].key).c_str(),L"Invalid stat",MB_ICONWARNING);return;}
        const std::string key=kStats[static_cast<size_t>(p.first)].key;
        if((key=="armor_pct"||key=="resistance_pct")&&v>=100){MessageBoxW(gMain,L"Armor and Resistance percentages must be below 100.",L"Invalid percentage",MB_ICONWARNING);return;}
        statValues[static_cast<size_t>(p.first)]=v;statProvided[static_cast<size_t>(p.first)]=true;
    }
    if(statValues[0]<=0||statValues[2]<=0){MessageBoxW(gMain,L"Health and Speed must be entered and must be greater than zero.",L"Required stats",MB_ICONWARNING);return;}
    int basics=0;for(const auto& a:gAbilityJson)if(a.find("\"kind\":\"basic\"")!=std::string::npos)++basics;
    if(basics!=1){MessageBoxW(gMain,L"Add exactly one ability with Type set to Basic before saving. You can add as many Special, Ultimate, Leader and Unique abilities as needed.",L"Basic ability required",MB_ICONWARNING);return;}
    std::error_code ec;fs::path folder=gDataDir/"user";fs::create_directories(folder,ec);
    if(ec){MessageBoxW(gMain,L"Could not create the data/user folder. Check folder permissions.",L"Save failed",MB_ICONERROR);return;}
    fs::path file=folder/(id+".json");
    if(fs::exists(file,ec)){MessageBoxW(gMain,L"A file already exists for this unit ID. Rename the unit or edit the existing JSON file.",L"Duplicate unit",MB_ICONWARNING);return;}
    std::ostringstream j;j<<"{\n  \"units\": [{\n    \"id\": \""<<escapeJson(id)<<"\",\n    \"name\": \""<<escapeJson(name)<<"\",\n    \"level\": "<<level<<",\n    \"tags\": [";
    auto tags=split(toUtf8(getText(gTags)));for(size_t i=0;i<tags.size();++i)j<<(i?",":"")<<"\""<<escapeJson(tags[i])<<"\"";
    j<<"],\n    \"stats\": {\n";
    bool firstStat=true;
    for(size_t i=0;i<std::size(kStats);++i)if(statProvided[i]){
        j<<(firstStat?"":",\n")<<"      \""<<kStats[i].key<<"\": "<<fmt(statValues[i]);firstStat=false;
    }
    if(!firstStat)j<<"\n";
    j<<"    },\n    \"abilities\": [\n";
    for(size_t i=0;i<gAbilityJson.size();++i)j<<"      "<<gAbilityJson[i]<<(i+1==gAbilityJson.size()?"\n":",\n");
    j<<"    ]\n  }]\n}\n";
    try{json::parse(j.str());}catch(const std::exception& e){MessageBoxW(gMain,fromUtf8(e.what()).c_str(),L"Unit data error",MB_ICONERROR);return;}
    {std::ofstream out(file,std::ios::binary);if(!out){MessageBoxW(gMain,L"Could not create the unit file.",L"Save failed",MB_ICONERROR);return;}out<<j.str();if(!out){out.close();fs::remove(file,ec);MessageBoxW(gMain,L"Writing the unit file failed.",L"Save failed",MB_ICONERROR);return;}}
    try{reloadData();}catch(const std::exception& e){fs::remove(file,ec);MessageBoxW(gMain,(std::wstring(L"The loader rejected this unit and the file was removed:\n\n")+fromUtf8(e.what())).c_str(),L"Validation failed",MB_ICONERROR);return;}
    MessageBoxW(gMain,(std::wstring(L"Unit saved and validated:\n")+fromUtf8(name)+L"\n\n"+fromUtf8(file.string())).c_str(),L"Unit added",MB_ICONINFORMATION);
    resetForm();
}

bool resolveTeam(const Content& c,const std::string& spec,std::vector<std::string>& ids,std::string& err) {
    if(const TeamDef* t=c.team(spec)){ids=t->units;return true;}
    ids=split(spec);if(ids.empty()){err="Enter a team ID or comma-separated unit IDs.";return false;}
    if(ids.size()>5){err="A team cannot contain more than five units.";return false;}
    for(const auto& id:ids)if(!c.units.count(id)){err="Unknown unit ID: "+id;return false;}
    return true;
}
struct SimResult {std::string text;};
void runSimulation() {
    if(gBusy)return;
    std::string a=toUtf8(trimW(getText(gSimA))),b=toUtf8(trimW(getText(gSimB)));
    int runs=0;try{runs=std::stoi(toUtf8(trimW(getText(gSimRuns))));}catch(...){}
    if(runs<1||runs>100000){MessageBoxW(gMain,L"Enter between 1 and 100,000 battles.",L"Invalid battle count",MB_ICONWARNING);return;}
    std::string seedText=toUtf8(trimW(getText(gSimSeed)));std::uint64_t seed=static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count())%1000000000ULL;
    if(!seedText.empty()){try{seed=std::stoull(seedText);}catch(...){MessageBoxW(gMain,L"Seed must be a whole number.",L"Invalid seed",MB_ICONWARNING);return;}}
    fs::path data=gDataDir;gBusy=true;EnableWindow(gSimButton,FALSE);setText(gSimOutput,L"Simulation running...");
    if(gWorker.joinable())gWorker.join();
    gWorker=std::thread([a,b,runs,seed,data](){
        auto result=std::make_unique<SimResult>();
        try{
            Content c=loadContent((data/"content.json").string());Rules rules;if(fs::exists(data/"rules.json"))rules=Rules::fromJson(json::parseFile((data/"rules.json").string()));
            std::vector<std::string> ta,tb;std::string err;if(!resolveTeam(c,a,ta,err)||!resolveTeam(c,b,tb,err))throw std::runtime_error(err);
            int wa=0,wb=0,draw=0;double turns=0;std::string trace;AiController ai;
            for(int i=0;i<runs;++i){Battle battle(c,rules,seed+static_cast<std::uint64_t>(i));battle.addTeam(0,ta);battle.addTeam(1,tb);battle.start();battle.logging=(i==0);battle.run(ai,ai);int win=battle.winner();if(win==0)++wa;else if(win==1)++wb;else ++draw;turns+=battle.turnsTaken;if(i==0){std::ostringstream t;for(const auto& line:battle.log)t<<line<<"\r\n";trace=t.str();}}
            std::ostringstream o;o<<"Simulation complete\r\nTeam A: "<<a<<"\r\nTeam B: "<<b<<"\r\nBattles: "<<runs<<"\r\nSeed: "<<seed<<"\r\n\r\n"
              <<std::fixed<<std::setprecision(1)<<"TEAM A WINS: "<<wa<<" ("<<100.0*wa/runs<<"%)\r\nTEAM B WINS: "<<wb<<" ("<<100.0*wb/runs<<"%)\r\nDRAWS: "<<draw<<" ("<<100.0*draw/runs<<"%)\r\n"
              <<std::setprecision(2)<<"AVERAGE TURNS: "<<turns/runs<<"\r\n\r\nFIRST BATTLE TRACE\r\n------------------\r\n"<<trace;
            result->text=o.str();
        }catch(const std::exception& e){result->text=std::string("Simulation failed:\r\n")+e.what();}
        PostMessageW(gMain,WM_APP+9,0,reinterpret_cast<LPARAM>(result.release()));
    });
}
void validateData() {
    try{reloadData();updateHome();std::string m="Validation passed.\n\nLoaded "+std::to_string(gContent->units.size())+" units, "+std::to_string(gContent->teams.size())+" teams and "+std::to_string(gContent->statuses.size())+" status definitions. The project loader checked JSON syntax and cross-references.";MessageBoxW(gMain,fromUtf8(m).c_str(),L"Data validation",MB_ICONINFORMATION);}
    catch(const std::exception& e){MessageBoxW(gMain,fromUtf8(e.what()).c_str(),L"Data validation failed",MB_ICONERROR);}
}
void chooseDataFolder() {
    BROWSEINFOW bi{};bi.hwndOwner=gMain;bi.lpszTitle=L"Choose the data folder containing content.json";bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi);if(!pid)return;
    wchar_t path[MAX_PATH]{};bool ok=SHGetPathFromIDListW(pid,path)!=FALSE;CoTaskMemFree(pid);
    if(!ok)return;fs::path p(path);if(!fs::exists(p/"content.json")){MessageBoxW(gMain,L"The selected folder does not contain content.json.",L"Not a GoHClone data folder",MB_ICONWARNING);return;}
    gDataDir=p;try{reloadData();showPage(0);}catch(const std::exception& e){MessageBoxW(gMain,fromUtf8(e.what()).c_str(),L"Data loading",MB_ICONERROR);}
}

std::string httpRequest(const std::wstring& url,const wchar_t* method,const std::string& body) {
    URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
    parts.dwSchemeLength=static_cast<DWORD>(-1);parts.dwHostNameLength=static_cast<DWORD>(-1);
    parts.dwUrlPathLength=static_cast<DWORD>(-1);parts.dwExtraInfoLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(url.c_str(),0,0,&parts))throw std::runtime_error("Invalid Comlink URL.");
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);
    if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    HINTERNET session=WinHttpOpen(L"GoHClone/ComlinkImporter",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session)throw std::runtime_error("Could not initialise Windows HTTP.");
    HINTERNET connection=WinHttpConnect(session,host.c_str(),parts.nPort,0);
    if(!connection){WinHttpCloseHandle(session);throw std::runtime_error("Could not connect to Comlink. Check that it is running.");}
    DWORD flags=parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0;
    HINTERNET request=WinHttpOpenRequest(connection,method,path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,flags);
    if(!request){WinHttpCloseHandle(connection);WinHttpCloseHandle(session);throw std::runtime_error("Could not create Comlink request.");}
    std::wstring headers=L"Content-Type: application/json\r\nAccept: application/json\r\n";
    BOOL sent=WinHttpSendRequest(request,headers.c_str(),static_cast<DWORD>(headers.size()),
        body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body.data()),static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),0);
    if(!sent||!WinHttpReceiveResponse(request,nullptr)){
        WinHttpCloseHandle(request);WinHttpCloseHandle(connection);WinHttpCloseHandle(session);
        throw std::runtime_error("Comlink did not return a response. Check its URL and availability.");
    }
    DWORD status=0,statusSize=sizeof(status);
    WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&statusSize,WINHTTP_NO_HEADER_INDEX);
    if(status<200||status>=300){
        WinHttpCloseHandle(request);WinHttpCloseHandle(connection);WinHttpCloseHandle(session);
        throw std::runtime_error("Comlink returned HTTP status "+std::to_string(status)+".");
    }
    std::string response;
    for(;;){DWORD available=0;if(!WinHttpQueryDataAvailable(request,&available)||available==0)break;
        std::string chunk(available,'\0');DWORD read=0;
        if(!WinHttpReadData(request,chunk.data(),available,&read))break;
        chunk.resize(read);response+=chunk;
    }
    WinHttpCloseHandle(request);WinHttpCloseHandle(connection);WinHttpCloseHandle(session);
    return response;
}
void syncComlink() {
    if(gBusy){MessageBoxW(gMain,L"Another task is already running.",L"Comlink",MB_ICONINFORMATION);return;}
    const wchar_t* envName=L"GOHCLONE_COMLINK_URL";
    DWORD n=GetEnvironmentVariableW(envName,nullptr,0);
    std::wstring base=L"http://localhost:3000";
    if(n){std::wstring v(n,L'\0');GetEnvironmentVariableW(envName,v.data(),n);v.resize(wcslen(v.c_str()));if(!v.empty())base=v;}
    while(!base.empty()&&base.back()==L'/')base.pop_back();
    if(MessageBoxW(gMain,(L"Download the latest SWGOH character and ability game-data segment from:\r\n\r\n"+base+
       L"\r\n\r\nComlink must already be running. Continue?").c_str(),L"SWGOH Comlink",MB_ICONQUESTION|MB_YESNO)!=IDYES)return;
    fs::path destination=gDataDir/"comlink";
    gBusy=true;
    std::thread([base,destination](){
        auto result=std::make_unique<ComlinkResult>();
        try{
            const std::string metadataText=httpRequest(base+L"/metadata",L"POST","{}");
            json::Value metadata=json::parse(metadataText);
            std::string version=metadata.str("latestGamedataVersion");
            if(version.empty())if(const auto* p=metadata.find("data"))version=p->str("latestGamedataVersion");
            if(version.empty())throw std::runtime_error("Comlink metadata did not contain latestGamedataVersion.");
            const std::string request="{\"payload\":{\"version\":\""+escapeJson(version)+"\",\"includePveUnits\":false,\"requestSegment\":2},\"enums\":false}";
            const std::string gameData=httpRequest(base+L"/data",L"POST",request);
            json::Value parsed=json::parse(gameData);
            if(!parsed.isObj())throw std::runtime_error("Comlink returned game data in an unexpected format.");
            fs::create_directories(destination);
            std::ofstream out(destination/"game-data.json",std::ios::binary|std::ios::trunc);
            if(!out)throw std::runtime_error("Could not write data/comlink/game-data.json.");
            out.write(gameData.data(),static_cast<std::streamsize>(gameData.size()));
            if(!out)throw std::runtime_error("Failed while saving the Comlink game-data file.");
            size_t units=0;
            if(const auto* u=parsed.find("units"))if(u->isArr())units=u->a.size();
            result->ok=true;
            result->text="Comlink game data downloaded successfully.\r\n\r\nGame-data version: "+version+
                "\r\nDownloaded: "+std::to_string(gameData.size()/1024)+" KB"+
                "\r\nUnit definitions in response: "+std::to_string(units)+
                "\r\n\r\nSaved to:\r\n"+(destination/"game-data.json").string()+
                "\r\n\r\nThis is the raw character/ability source cache. Character import and stat/ability conversion are separate steps; the raw game data is not yet a ready-to-run GoHClone roster.";
        }catch(const std::exception& e){result->text=std::string("Comlink sync failed:\r\n")+e.what();}
        PostMessageW(gMain,WM_APP+10,0,reinterpret_cast<LPARAM>(result.release()));
    });
}

void navigate(int p){showPage(p);}
void appendMenuItem(HMENU menu,UINT id,const wchar_t* label) {
    AppendMenuW(menu,MF_OWNERDRAW,id,reinterpret_cast<LPCWSTR>(label));
}
HMENU createMenuBar() {
    HMENU root=CreateMenu(),file=CreatePopupMenu(),roster=CreatePopupMenu(),battle=CreatePopupMenu(),help=CreatePopupMenu();
    appendMenuItem(file,M_DATA_COMLINK,L"Sync SWGOH game data (Comlink)...");
    appendMenuItem(file,M_FILE_SELECT,L"Choose data folder...");
    appendMenuItem(file,M_FILE_RELOAD,L"Reload project data");
    AppendMenuW(file,MF_SEPARATOR,0,nullptr);appendMenuItem(file,M_FILE_EXIT,L"Exit");
    appendMenuItem(roster,M_ROSTER_HOME,L"Holotable overview");
    appendMenuItem(roster,M_ROSTER_ADD,L"Create new unit");
    appendMenuItem(roster,M_ROSTER_STATUS,L"Buff and debuff archive");
    appendMenuItem(battle,M_BATTLE_SIM,L"Combat simulator");
    appendMenuItem(battle,M_BATTLE_VALIDATE,L"Validate game data");
    appendMenuItem(help,M_HELP_ABOUT,L"About GoHClone");
    AppendMenuW(root,MF_POPUP|MF_OWNERDRAW,reinterpret_cast<UINT_PTR>(file),reinterpret_cast<LPCWSTR>(L"DATA"));
    AppendMenuW(root,MF_POPUP|MF_OWNERDRAW,reinterpret_cast<UINT_PTR>(roster),reinterpret_cast<LPCWSTR>(L"ROSTER"));
    AppendMenuW(root,MF_POPUP|MF_OWNERDRAW,reinterpret_cast<UINT_PTR>(battle),reinterpret_cast<LPCWSTR>(L"COMBAT"));
    AppendMenuW(root,MF_POPUP|MF_OWNERDRAW,reinterpret_cast<UINT_PTR>(help),reinterpret_cast<LPCWSTR>(L"ARCHIVES"));
    MENUINFO mi{sizeof(mi)};mi.fMask=MIM_BACKGROUND;mi.hbrBack=gMenuBrush;
    SetMenuInfo(file,&mi);SetMenuInfo(roster,&mi);SetMenuInfo(battle,&mi);SetMenuInfo(help,&mi);
    return root;
}
LRESULT CALLBACK wndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg){
    case WM_CREATE:{
        gMain=hwnd;
        gFont=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        gBackgroundBrush=CreateSolidBrush(kSpaceBlack);gPanelBrush=CreateSolidBrush(kPanel);gEditBrush=CreateSolidBrush(kInput);gMenuBrush=CreateSolidBrush(kPanel);
        SetMenu(hwnd,createMenuBar());
        showPage(0);return 0;
    }
    case WM_ERASEBKGND:{RECT r{};GetClientRect(hwnd,&r);FillRect(reinterpret_cast<HDC>(wp),&r,gBackgroundBrush);return 1;}
    case WM_CTLCOLORSTATIC:{
        HDC dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,kText);SetBkColor(dc,kSpaceBlack);SetBkMode(dc,TRANSPARENT);return reinterpret_cast<LRESULT>(gBackgroundBrush);
    }
    case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORSCROLLBAR:{
        HDC dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,kText);SetBkColor(dc,kInput);return reinterpret_cast<LRESULT>(gEditBrush);
    }
    case WM_CTLCOLORBTN:{
        HDC dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,kGold);SetBkColor(dc,kPanel);return reinterpret_cast<LRESULT>(gPanelBrush);
    }
    case WM_MEASUREITEM:{
        auto* m=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if(m->CtlType==ODT_MENU){const wchar_t* label=reinterpret_cast<const wchar_t*>(m->itemData);HDC dc=GetDC(hwnd);SIZE z{};HFONT old=reinterpret_cast<HFONT>(SelectObject(dc,gFont));GetTextExtentPoint32W(dc,label,static_cast<int>(wcslen(label)),&z);SelectObject(dc,old);ReleaseDC(hwnd,dc);m->itemWidth=static_cast<UINT>(z.cx+38);m->itemHeight=34;return TRUE;}
        break;
    }
    case WM_DRAWITEM:{
        auto* d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(d->CtlType==ODT_MENU){const wchar_t* label=reinterpret_cast<const wchar_t*>(d->itemData);const bool selected=(d->itemState&ODS_SELECTED)!=0;
            HBRUSH brush=CreateSolidBrush(selected?kSelected:kPanel);FillRect(d->hDC,&d->rcItem,brush);DeleteObject(brush);
            SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,selected?kGold:kText);HFONT old=reinterpret_cast<HFONT>(SelectObject(d->hDC,gFont));
            RECT r=d->rcItem;r.left+=18;DrawTextW(d->hDC,label,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_LEFT);
            SelectObject(d->hDC,old);return TRUE;}
        break;
    }
    case WM_COMMAND:{
        int id=LOWORD(wp),code=HIWORD(wp);
        if(id==M_DATA_COMLINK){syncComlink();return 0;}
        if(id==M_FILE_SELECT){chooseDataFolder();return 0;}
        if(id==M_FILE_RELOAD){try{reloadData();showPage(0);}catch(const std::exception& e){MessageBoxW(hwnd,fromUtf8(e.what()).c_str(),L"Reload failed",MB_ICONERROR);}return 0;}
        if(id==M_FILE_EXIT){DestroyWindow(hwnd);return 0;}
        if(id==M_ROSTER_HOME||(id==ID_HOME_ADD&&code==BN_CLICKED)){navigate(id==M_ROSTER_HOME?0:1);return 0;}
        if(id==M_ROSTER_ADD){navigate(1);return 0;}
        if(id==M_ROSTER_STATUS||(id==ID_HOME_STATUS&&code==BN_CLICKED)){navigate(2);return 0;}
        if(id==M_BATTLE_SIM||(id==ID_HOME_SIM&&code==BN_CLICKED)){navigate(3);return 0;}
        if(id==M_BATTLE_VALIDATE){validateData();return 0;}
        if(id==M_HELP_ABOUT){MessageBoxW(hwnd,L"GoHClone Desktop\r\nA desktop front end for the GoHClone C++ battle engine.\r\nOriginal demo content only; add your own test units through Roster > Create new unit.",L"About",MB_ICONINFORMATION);return 0;}
        if(id==ID_NAME&&code==EN_CHANGE){updateNameId();return 0;}
        if(id==ID_ADD_EFFECT&&code==BN_CLICKED){addEffect();return 0;}
        if(id==ID_REMOVE_EFFECT&&code==BN_CLICKED){removeEffect();return 0;}
        if(id==ID_ADD_ABILITY&&code==BN_CLICKED){addAbility();return 0;}
        if(id==ID_REMOVE_ABILITY&&code==BN_CLICKED){removeAbility();return 0;}
        if(id==ID_SAVE_UNIT&&code==BN_CLICKED){saveUnit();return 0;}
        if(id==ID_CLEAR_UNIT&&code==BN_CLICKED){resetForm();return 0;}
        if(id==ID_STATUS_LIST&&code==LBN_SELCHANGE){updateStatusDetail();return 0;}
        if((id==ID_STATUS_FILTER&&code==CBN_SELCHANGE)||(id==ID_STATUS_SEARCH&&code==EN_CHANGE)){refreshStatusList();return 0;}
        if(id==ID_SIM_RUN&&code==BN_CLICKED){runSimulation();return 0;}
        break;
    }
    case WM_APP+10:{
        std::unique_ptr<ComlinkResult> result(reinterpret_cast<ComlinkResult*>(lp));gBusy=false;
        if(result)MessageBoxW(hwnd,fromUtf8(result->text).c_str(),result->ok?L"Comlink sync complete":L"Comlink sync failed",result->ok?MB_ICONINFORMATION:MB_ICONERROR);
        return 0;
    }
    case WM_APP+9:{
        std::unique_ptr<SimResult> result(reinterpret_cast<SimResult*>(lp));
        if(gWorker.joinable())gWorker.join();gBusy=false;if(gSimButton)EnableWindow(gSimButton,TRUE);
        if(gSimOutput&&result)setText(gSimOutput,fromUtf8(result->text));return 0;
    }
    case WM_GETMINMAXINFO:{auto* m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize.x=1260;m->ptMinTrackSize.y=930;return 0;}
    case WM_DESTROY:
        if(gWorker.joinable())gWorker.join();
        if(gFont)DeleteObject(gFont);
        if(gBackgroundBrush)DeleteObject(gBackgroundBrush);if(gPanelBrush)DeleteObject(gPanelBrush);
        if(gEditBrush)DeleteObject(gEditBrush);if(gMenuBrush)DeleteObject(gMenuBrush);
        PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    const HRESULT comResult=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc),ICC_STANDARD_CLASSES};InitCommonControlsEx(&icc);
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);gExeDir=fs::path(exe).parent_path();gDataDir=locateData();
    WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=wndProc;wc.hInstance=instance;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hbrBackground=nullptr;wc.lpszClassName=kWindowClass;
    if(!RegisterClassExW(&wc))return 1;
    HWND w=CreateWindowExW(0,kWindowClass,L"GoHClone // Galactic Combat Analysis",WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,CW_USEDEFAULT,1280,1010,nullptr,nullptr,instance,nullptr);
    if(!w)return 1;ShowWindow(w,show);UpdateWindow(w);
    MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}
    if(SUCCEEDED(comResult))CoUninitialize();
    return static_cast<int>(m.wParam);
}
