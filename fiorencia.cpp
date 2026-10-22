// ============================================================================
//  FIORENCIA: ART IS WAR  --  a Renaissance fantasy life-sim, 8-bit terminal
//
//  Build (release, small binary):
//      g++ -std=c++17 -Os -s -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -Wl,--gc-sections -o fiorencia.exe fiorencia.cpp
//
//  Build (debug twin -- keep this next to the release binary):
//      g++ -std=c++17 -O2 -g -o fiorencia.debug.exe fiorencia.cpp
//      cp fiorencia.debug.exe fiorencia.exe && strip fiorencia.exe
//
//  Run:     ./fiorencia [--amber] [--plain] [--seed N] [--plot N] [--day N]
//
//    --amber   amber phosphor instead of green
//    --plain   no ANSI colour and no screen clearing (handy for piping/logs)
//    --seed N  fixed RNG seed (this also fixes which story is drawn)
//    --plot N  force the story of a new game: 0 revolt, 1 plague, 2 Albizzi
//    --day N   start a new game on day N (the day 10/50/100 beats then play
//              on the next three nights, handy for testing)
//
//  Save file: fiorencia.sav (written at the end of every day).
//
//  BUILD NOTES -- read before editing:
//    - Compiled with -fno-exceptions and -fno-rtti. This file contains no
//      try / catch / throw / dynamic_cast / typeid, and must not gain any.
//    - Do not introduce standard library calls that throw on bad input:
//        std::stoi / stol / stoll / stod / stof
//        std::string::at() / std::vector::at() / std::map::at()
//        std::bitset::to_ulong() and similar
//      Use atoi / atof / strtoul (C, non-throwing) or operator[] instead.
//    - All I/O is plain stdio (out / outf / getLine, fopen / fscanf). Do NOT
//      include <iostream>, <fstream> or <sstream>: in a static build they pull
//      in the locale machinery and add several hundred KB to the exe.
//    - random_device{}() can throw on entropy-starved systems; the RNG is
//      therefore seeded from the wall clock. See the rng initializer below.
//    - Violating any of the above at runtime yields std::terminate (abort).
//      Loud, not silent -- but it will not warn you at compile time.
//    - The ship binary is stripped (-s); keep fiorencia.debug.exe around
//      for gdb / gprof / crash-dump symbolisation.
// ============================================================================
#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

using namespace std;

// stdio output/input helpers. Deliberately no <iostream>/<fstream>/<sstream>:
// they drag the whole locale machinery into a static build (>1 MB of exe).
static void out(const string &s) { fwrite(s.data(), 1, s.size(), stdout); }
static void outf(const string &s) { out(s); fflush(stdout); }
// std::getline semantics: false only if EOF hit before any character was read
static bool getLine(FILE *f, string &s) {
    s.clear();
    int c;
    bool any = false;
    while ((c = fgetc(f)) != EOF) {
        any = true;
        if (c == '\n') return true;
        s += (char)c;
    }
    return any;
}

// ----------------------------------------------------------------------------
//  Globals / dice
// ----------------------------------------------------------------------------
// Seeded from the wall clock rather than random_device: under -fno-exceptions,
// a throwing random_device would call terminate, and on Windows it delivers no
// better entropy than the clock anyway. --seed N still overrides it in main().
static mt19937 rng{(unsigned)chrono::steady_clock::now().time_since_epoch().count()};
static int d(int n) { return uniform_int_distribution<int>(1, n)(rng); }
static bool g_color = true;
static bool g_clear = true;
static string g_on = "\033[32m";
static int g_forcePlot = -1;  // --plot N
static int g_startDay = 1;    // --day N
static string savePath(const string &name, int slot) {
    string safe;
    for (unsigned char c : name) {
        if (isalnum(c) || c == '-' || c == '_') safe += (char)c;
        else if (c == ' ') safe += '_';
    }
    if (safe.empty()) safe = "Citizen";
    return "fiorencia_" + safe + "_" + to_string(slot) + ".sav";
}

static void resetTerm() {
    if (g_color) outf("\033[0m");
}

// ----------------------------------------------------------------------------
//  String / UI helpers  (box-drawing, 62-col inner width)
// ----------------------------------------------------------------------------
static const int W = 62;

static size_t ulen(const string &s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) n++;
    return n;
}
static string rep(const string &s, int n) {
    string r;
    for (int i = 0; i < n; i++) r += s;
    return r;
}
static string trim(const string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
// first whitespace-delimited word, then the (trimmed) rest of the line
static void splitCmd(const string &line, string &cmd, string &arg) {
    size_t a = 0, n = line.size();
    while (a < n && isspace((unsigned char)line[a])) a++;
    size_t b = a;
    while (b < n && !isspace((unsigned char)line[b])) b++;
    cmd = line.substr(a, b - a);
    arg = trim(line.substr(b));
}
static string lower(string s) {
    for (auto &c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static string pad3(int n) {
    char b[16];
    snprintf(b, sizeof b, "%03d", n);
    return b;
}
static string fmtF(int df) {  // florins are stored in tenths
    char b[32];
    if (df % 10 == 0) snprintf(b, sizeof b, "%d", df / 10);
    else snprintf(b, sizeof b, "%d.%d", df / 10, df % 10);
    return b;
}
static string bar(int cur, int mx, int len) {
    int f = mx > 0 ? max(0, min(len, cur * len / mx)) : 0;
    if (cur > 0 && f == 0) f = 1;
    return rep("#", f) + rep(".", len - f);
}

static void top() { out("+" + rep("-", W) + "+\n"); }
static void mid() { out("+" + rep("-", W) + "+\n"); }
static void bot() { out("+" + rep("-", W) + "+\n"); }
static void row(const string &s = "") {
    string t = " " + s;
    int n = (int)ulen(t);
    out("|" + t + string(max(0, W - n), ' ') + "|\n");
}
// Word-wrapped rows (ASCII text only)
static void rows(const string &s, const string &indent = "") {
    string w, line = indent;
    bool first = true;
    size_t i = 0, n = s.size();
    while (true) {
        while (i < n && isspace((unsigned char)s[i])) i++;
        if (i >= n) break;
        size_t j = i;
        while (j < n && !isspace((unsigned char)s[j])) j++;
        w = s.substr(i, j - i);
        i = j;
        if (!first && line.size() + 1 + w.size() > 58) {
            row(line);
            line = indent;
            first = true;
        }
        line += (first ? "" : " ") + w;
        first = false;
    }
    row(line);
}
static void title(const string &t) {
    top();
    row(t);
    mid();
}
static void clearScr() {
    if (g_clear) out("\033[2J\033[H");
}

static string readLine() {
    string s;
    if (!getLine(stdin, s)) {
        out("\n!! LINE DROPPED. SESSION ENDED.\n");
        exit(0);
    }
    return s;
}
static string prompt() {
    outf("> ");
    return lower(trim(readLine()));
}
static void pause() {
    outf("[ENTER] CONTINUE ");
    readLine();
}
static char ask(const string &msg, const string &allowed) {
    while (true) {
        outf(msg);
        string s = lower(trim(readLine()));
        if (!s.empty() && allowed.find(s[0]) != string::npos) return s[0];
    }
}

// ----------------------------------------------------------------------------
//  Data tables
// ----------------------------------------------------------------------------
static const char *CLS[3] = {"ARTISAN", "KNIGHT", "SCHOLAR"};
static const char *WPN[4] = {"Fists", "Dagger", "Longsword", "Crossbow"};
static const int WPN_DIE[4] = {2, 4, 8, 6};
static const char *ARM[3] = {"None", "Leather Jerkin", "Brigandine (heavy)"};
static const char *SPL[3] = {"Firebolt", "Heal", "Shield"};
static const char *POT[5] = {"Healing Salve", "Fire Oil", "Stoneskin Draught",
                             "Night-Eye Tincture", "Antidote"};
static const char *POT_KEY[5] = {"healing", "fireoil", "stoneskin", "nighteye", "antidote"};
static const int POT_HERBS[5] = {2, 1, 2, 1, 1};

// Rituals: 0 weapon, 1 tongues, 2 familiar, 3 ward, 4 body, 5 lore, 6 wits
static const char *RK[7] = {"weapon", "tongues", "familiar", "ward", "boon", "loreboon", "witsboon"};
static const char *RN[7] = {"Weapon Enchantment", "Gift of Tongues", "Bound Familiar",
                            "Ward of Warding", "Boon of Might (+1 BODY)",
                            "Boon of Insight (+1 LORE)", "Boon of Cunning (+1 WITS)"};
static const int RC[7] = {150, 200, 250, 300, 500, 500, 500};  // tenths of florins

// ----------------------------------------------------------------------------
//  Pacing dials
// ----------------------------------------------------------------------------
enum Plot { PLOT_REVOLT, PLOT_SORCERER, PLOT_ALBIZZI, N_PLOTS };

// ----------------------------------------------------------------------------
//  The economy is tuned from this block.  A campaign runs in three phases:
//    SURVIVAL    (days  ~1-15)  wages only just outrun the cost of living; the first
//                               florins go on rations, a blade and craft materials
//    INVESTMENT  (days ~15-70)  gear, pacts and craft ranks pay for themselves, and the
//                               story acts open windows and shortages in the markets
//    WEALTH      (days  ~70+)   the engines run and the legacy purse fills
//  Everything is derived from fields already in the save file (day, florins, class, story
//  state): there is no new persistent state, and old saves load and play as before.
// ----------------------------------------------------------------------------
static const int LEGACY_DF     = 5000; // base price of a legacy, tenths of a florin (500f)
static const int LEGACY_SPREAD = 10;   // % the ending moves it: a scarred city charges more, a prospering one less
static const int WAGE_PCT      = 160;  // work: 1d20 x 10p x this %
static const int INCOME_PCT    = 200;  // study, craft sales, patrons, windfalls and loot, in %
static const int UPKEEP_BASE   = 110;  // p/day on day 1 ...
static const int UPKEEP_RISE   = 4;    // ... plus this much for every 10 days ...
static const int UPKEEP_WEALTH = 300;  // ... plus 1p for every this many tenths of a florin you hold
static const int CRAFT_MAT[4]   = {40, 50, 60, 60};  // Artisan materials per craft at rank 0, 1, 2, 3 (piccioli)
static const int CRAFT_TOOLS[3] = {80, 200, 450};    // tools for rank 1, 2, 3, bought with florins (tenths) by 'craft'
static const int CRAFT_K        = 50;  // craft sales: (1d20 + WITS) x (CRAFT_K + CRAFT_STEP x rank) / 10 x INCOME_PCT
static const int CRAFT_STEP     = 15;
static const int START_P[3]    = {550, 250, 400};  // Artisan, Knight, Scholar
static const int START_DF[3]   = {30, 40, 40};     // tenths of a florin

static int inc(int v) { return v * INCOME_PCT / 100; }
static int wage(int v) { return v * WAGE_PCT / 100; }
static int pct(int v, int shift) { return v * (100 + shift) / 100; }

// Percent shifts in the city's mood: +40 means 40% more (or better).  Set by story choices.
struct Eco { int upkeep, wage, patron, loot, herb; };

struct Shop {
    const char *key;
    const char *name;
    int cost;
    bool flor;
    int kind;  // 0 weapon, 1 armor, 2 spell, 3 rations, 4 herbs
    int id;
};
static const Shop SHOP[] = {
    {"dagger", "Dagger  (1d4)", 12, true, 0, 1},
    {"longsword", "Longsword  (1d8)", 75, true, 0, 2},
    {"crossbow", "Crossbow  (1d6, ranged)", 50, true, 0, 3},
    {"leather", "Leather Jerkin  (+1 DEF)", 50, true, 1, 1},
    {"brigandine", "Brigandine, heavy  (+3 DEF)", 150, true, 1, 2},
    {"firebolt", "Spell: Firebolt  (2d6+LORE)", 125, true, 2, 0},
    {"heal", "Spell: Heal  (1d6+2 HP)", 200, true, 2, 1},
    {"shield", "Spell: Shield  (+3 DEF, 1 turn)", 150, true, 2, 2},
    {"rations", "Rations, 5 days of upkeep", 0, false, 3, 0},   // priced from today's upkeep, see shopPrice
    {"herbs", "3 common herbs", 30, false, 4, 0},
};
static const int NSHOP = sizeof(SHOP) / sizeof(SHOP[0]);

// ----------------------------------------------------------------------------
//  Player
// ----------------------------------------------------------------------------
struct Player {
    string name = "Citizen";
    int cls = 0, body = 5, wits = 5, lore = 5;
    int hp = 15, sp = 15, p = 0, df = 0;
    int day = 1, ap = 3, rate = 120;
    int weapon = 0, armor = 0;
    int spell[3] = {0, 0, 0};
    int herbs = 0;
    int potion[5] = {0, 0, 0, 0, 0};
    int rations = 0, crafted = 0, ench = 0;
    int familiar = 0, gift = 0, ward = 0, poisoned = 0;
    int prepared = -1, oil = 0, stoneskin = 0, worked = 0, nighteye = 0;
    int insight = 0;
    int saveSlot = 1;
    // story: one of three plots per playthrough
    int plot = -1, plotStage = 0, plotWon = 0, plotLost = 0, plotEndTier = 0;
    int plotChoice[3] = {0, 0, 0};   // branch taken on day 10 / 50 / 100 (0 = beat not played yet)

    int maxhp() const { return 10 + body; }
    int maxsp() const { return 10 + body; }
    int primary() const { return cls == 0 ? wits : cls == 1 ? body : lore; }
    int mundMod() const { return cls == 1 ? 2 : cls == 2 ? -2 : 0; }
    int magMod() const { return cls == 1 ? -2 : cls == 2 ? 2 : 0; }
    int natMod() const { return cls == 1 ? -2 : 0; }
    int ritMod() const { return cls == 1 ? -2 : cls == 2 ? 2 : 0; }
    int loreDiscount() const { return cls == 2 ? min(lore, 10) * 3 / 2 : min(lore, 10) / 2; }  // % off market
};

template <class F>
static void fields(Player &P, F &&f) {
    f(P.cls); f(P.body); f(P.wits); f(P.lore); f(P.hp); f(P.sp); f(P.p); f(P.df);
    f(P.day); f(P.ap); f(P.rate); f(P.weapon); f(P.armor);
    for (int &x : P.spell) f(x);
    f(P.herbs);
    for (int &x : P.potion) f(x);
    f(P.rations); f(P.crafted); f(P.ench); f(P.familiar); f(P.gift); f(P.ward);
    f(P.poisoned); f(P.prepared); f(P.oil); f(P.stoneskin); f(P.worked); f(P.nighteye);
    f(P.insight);
}

static bool saveGame(Player &P) {
    FILE *o = fopen(savePath(P.name, P.saveSlot).c_str(), "w");
    if (!o) return false;
    fprintf(o, "%s\n", P.name.c_str());
    fields(P, [&](int &x) { fprintf(o, "%d ", x); });
    fputs("\n", o);
    fprintf(o, "%d %d %d %d %d", P.plot, P.plotStage, P.plotWon, P.plotLost, P.plotEndTier);
    for (int x : P.plotChoice) fprintf(o, " %d", x);
    fputs("\n", o);
    return fclose(o) == 0;
}
static bool loadGame(Player &P) {
    FILE *in = fopen(savePath(P.name, P.saveSlot).c_str(), "r");
    if (!in) return false;
    bool ok = true;
    auto rd = [&](int &x) { if (ok && fscanf(in, "%d", &x) != 1) ok = false; };
    getLine(in, P.name);
    fields(P, rd);
    if (!ok) { fclose(in); return false; }
    int pl = -1;
    if (fscanf(in, "%d", &pl) == 1) {  // current format: story state follows
        P.plot = pl;
        rd(P.plotStage); rd(P.plotWon); rd(P.plotLost); rd(P.plotEndTier);
        for (int &x : P.plotChoice) rd(x);
        if (!ok || P.plot < 0 || P.plot >= N_PLOTS) { fclose(in); return false; }
    } else {         // older save: roll a story now; the beats catch up night by night
        P.plot = d(N_PLOTS) - 1;
        P.plotStage = P.plotWon = P.plotLost = P.plotEndTier = 0;
        for (int &x : P.plotChoice) x = 0;
    }
    fclose(in);
    return true;
}

// A claimed legacy is remembered by one extra trailing line on the save file.
// saveGame()'s own output and loadGame() are unchanged: older builds just ignore
// the extra line, and the next saveGame() to that slot writes the file afresh.
static const char *LEGACY_MARK = "LEGACY CLAIMED";
static void markLegacy(const Player &P) {
    FILE *o = fopen(savePath(P.name, P.saveSlot).c_str(), "a");
    if (!o) return;
    fprintf(o, "%s\n", LEGACY_MARK);
    fclose(o);
}
static bool legacyClaimed(const Player &P) {
    FILE *in = fopen(savePath(P.name, P.saveSlot).c_str(), "r");
    if (!in) return false;
    string s, last;
    int n = 0;
    while (getLine(in, s)) { last = s; n++; }
    fclose(in);
    return n >= 4 && last == LEGACY_MARK;
}

// ----------------------------------------------------------------------------
//  Message log (shown on the day screen)
// ----------------------------------------------------------------------------
static vector<string> g_log;
static void say(const string &s) {
    g_log.push_back(s);
    if (g_log.size() > 7) g_log.erase(g_log.begin());
}

// ----------------------------------------------------------------------------
//  Story: one of three plots is drawn per playthrough
//    - the clock is the calendar: news every 5 days and a beat on day 10, 50
//      and 100, whatever the player is doing
//    - only fights against the ACTIVE plot's NPCs move the win/loss record;
//      every other NPC still turns up, as flavour
//    - the record (tier) picks the version of each beat that plays; the choice
//      made at the beat sets its consequences
// ----------------------------------------------------------------------------
static const int BEAT_DAY[3] = {10, 50, 100};

static const char *PLOT_NAME[N_PLOTS] = {"THE GRAIN REVOLT", "THE MILANESE PLAGUE",
                                         "THE ALBIZZI CONSPIRACY"};

// News on the night screen every 5 days: RUMOR[plot][stage][alternate]
static const char *RUMOR[N_PLOTS][4][2] = {
    {   // THE GRAIN REVOLT
        {"Grain prices climb again, and the farmers mutter at the market gates.",
         "Carts are stopped on the Pisa road, and the drivers come back without their loads."},
        {"Farmers drill with scythes in the hills. Some of them wear brigand red.",
         "The name of Mona Tessa is chalked on walls from the Mercato to the Arno."},
        {"The Signoria hires more condottieri, and the granaries are under guard.",
         "Heralds ride between the Palazzo and the farmers' camps, and nobody trusts them."},
        {"Songs about the revolt are sung in the taverns, and some are banned.",
         "The city argues over who won, and the market stalls fill with grain again."}},
    {   // THE MILANESE PLAGUE
        {"A cough runs through Santa Croce, and the dead are buried at night.",
         "Dogs and wolves come into the city, and they do not shy from torches."},
        {"A Milanese physician sells a cure in the Oltrarno, and his apothecaries multiply.",
         "Beasts with weeping eyes prowl the lanes. Doors are barred by dusk."},
        {"Wells are chalked with crosses, but the sick are many.",
         "They say the Maestro's cellars glow green at night."},
        {"The city counts its dead and argues over who is to blame.",
         "Fewer beasts are seen at night, but nobody trusts the water."}},
    {   // THE ALBIZZI CONSPIRACY
        {"Two more duels in the piazza this week, and the dead were Signoria clerks.",
         "Men with gilt-hilted blades drink free at the Albizzi tavern."},
        {"The Guild of the Gilded Blade has taken new rooms near the Albizzi palazzo.",
         "A Signoria clerk vanished, and his seal turned up on an Albizzi letter."},
        {"Ser Ugo's household guard has doubled, and so has the Guild.",
         "Gonfalonieri sleep with a blade under the pillow."},
        {"Duels are rarer now, and politer. The city is deciding who rules it.",
         "Ser Ugo's name is spoken in whispers, whether he won or fled."}}};

// One choice at a beat. Florins: df0 + dfT * tier, in tenths (negative = you pay).
// eAtk / eHp: from now on this plot's NPCs hit harder / tougher (or softer) because of the choice.
struct Opt {
    const char *label, *result;
    int df0, dfT, hp, lose, rations, herbs, stat, eAtk, eHp;   // stat: 1 BODY, 2 WITS, 3 LORE (+1)
    Eco eco;   // upkeep, wage, patron, loot, herb: the city's mood in the act this choice shapes
};
struct Beat {
    const char *scene[3];   // by tier: 0 the plot is winning, 1 contested, 2 you are on top
    Opt opt[2];
};

//                      label,  result,   df0, dfT, hp, lose, rations, herbs, stat, eAtk, eHp,  {upkeep, wage, patron, loot, herb}
//  Payouts: day 10 as before (the purse is small), day 50 x2 and day 100 x2 (they were tuned for a 50f goal).
static const Beat BEAT[N_PLOTS][3] = {
    {   // ---------------------------------------------------------- THE GRAIN REVOLT
        {   // day 10: grain money
            {"Hunger has turned to fury. Brigands wear the farmers' red rag now, and the Pisa road "
             "belongs to them. Mona Tessa, a miller's widow, asks you for grain money.",
             "Hunger and banditry are tangled together. Mona Tessa, a miller's widow, speaks for the "
             "farmers, but half her men are brigands. She asks you for grain money.",
             "You have bloodied the brigands, and the farmers noticed. Mona Tessa, a miller's widow, "
             "comes to you instead of to them, and asks for grain money."},
            {{"Fund the grain wagons", "Mona Tessa's wagons roll, and her scouts will remember you.",
              -200, 50, 0, 0, 0, 0, 0, -1, 0, {-25, 0, 0, 0, 0}},
             {"Sell their camp to the Signoria",
              "The Signoria pays in new florins. By dawn the farmers are burning barns.",
              150, 50, 0, 0, 0, 0, 0, 1, 2, {35, 0, 0, 25, 0}}}},
        {   // day 50: the march on the gates
            {"Thousands march on the Porta San Frediano with brigands at their head. The guard is thin, "
             "and the gate-captain looks to you.",
             "A column of farmers and brigands marches on the Porta San Frediano. The guard is split, "
             "and the gate-captain looks to you.",
             "Without their brigands the farmers' march is ragged, and the guard might hold. The "
             "gate-captain looks to you all the same."},
            {{"Open the gate to the marchers", "The granary is shared out, and you are cut in the crush.",
              0, 0, -2, 0, 6, 0, 0, -1, 0, {-30, 0, -20, 0, 0}},
             {"Hold the gate for the Signoria",
              "The line holds, barely. The Signoria pays, and the farmers will not forget.",
              400, 200, -3, 1, 0, 0, 0, 1, 0, {30, 0, 30, 20, 0}}}},
        {   // day 100: the settlement
            {"The Signoria and the rebels meet in the Palazzo, with the brigands still at large. Both "
             "sides want you to speak for them.",
             "The Signoria and the rebels meet in the Palazzo to settle terms. Both sides want you to "
             "speak for them.",
             "The brigands are broken, and the Signoria and the rebels meet in the Palazzo to settle "
             "terms. Both sides want you to speak for them."},
            {{"Speak for the farmers", "The Palazzo hears the commons. You are named a friend of the farms.",
              200, 200, 0, 0, 0, 0, 1, 0, 0, {-20, 10, 0, 0, 0}},
             {"Speak for the Signoria", "The Signoria signs, and you are paid. The farms are quiet, for now.",
              400, 300, 0, 0, 0, 0, 0, 0, 0, {10, 0, 30, 0, 0}}}}},
    {   // ---------------------------------------------------------- THE MILANESE PLAGUE
        {   // day 10: the cure
            {"Fever fills Santa Croce and mad beasts prowl the lanes. A Milanese physician, Maestro "
             "Ambrogio, offers a miracle cure, for a price.",
             "Fever spreads in Santa Croce, and mad beasts are seen at night. A Milanese physician, "
             "Maestro Ambrogio, offers a cure for a price.",
             "You have cut down the mad beasts, and the fever slows. A Milanese physician, Maestro "
             "Ambrogio, still offers his cure and watches you closely."},
            {{"Burn the infected houses", "Santa Croce burns. The fever slows, and the survivors curse you.",
              -150, 50, -1, 0, 0, 0, 0, 0, -2, {0, -10, -20, 0, 0}},
             {"Buy the Maestro's cure",
              "The vial heals you at once. The Maestro smiles, and the beasts grow bolder.",
              -100, 0, 3, 0, 0, 0, 0, 1, 0, {10, 0, 0, 15, -30}}}},
        {   // day 50: the cellar
            {"The plague has taken the Oltrarno, and the Maestro's apothecaries are everywhere. His "
             "cellar lies under the old hospice.",
             "The plague is in three quarters, and the Maestro's cure sells briskly. His cellar under "
             "the old hospice is known, and guarded.",
             "You have thinned the beasts, and the plague spreads slowly now. The Maestro knows you "
             "know his cellar under the old hospice."},
            {{"Raid the Maestro's cellar", "You break his laboratory open and carry out what herbs survive.",
              0, 0, -3, 0, 0, 4, 0, -1, 0, {0, 10, 0, 0, 50}},
             {"Take his coin and look away", "Coin for silence. The wells of San Niccolo turn within the week.",
              400, 200, 0, 0, 0, 0, 0, 1, 2, {35, -10, 0, 0, 0}}}},
        {   // day 100: the great working
            {"The Maestro begins his great working in the Duomo crypt, and the plague beasts guard its "
             "doors. The city is nearly empty.",
             "The Maestro begins his great working in the Duomo crypt. The beasts still guard its doors, "
             "and the city holds its breath.",
             "The beasts are mostly gone, and the Maestro begins his great working alone in the Duomo "
             "crypt. The city holds its breath."},
            {{"Break his circle by force", "You break the circle at a cost. The crypt shakes, and the Maestro runs.",
              200, 200, -4, 0, 0, 0, 1, 0, 0, {-15, 10, 0, 0, 20}},
             {"Bargain for a share of his power",
              "The Maestro names his price, and new knowledge settles in your mind.",
              400, 300, 0, 0, 0, 0, 3, 0, 0, {0, 0, 20, 0, 30}}}}},
    {   // ---------------------------------------------------------- THE ALBIZZI CONSPIRACY
        {   // day 10: the sealed letter
            {"Duels in the piazza have turned to murders, and the Guild of the Gilded Blade wears Albizzi "
             "colours openly. Ser Ugo's steward asks you to carry a sealed letter.",
             "Quarrels in the piazza turn to blades more often. The Guild of the Gilded Blade seems to "
             "serve Ser Ugo degli Albizzi, whose steward asks you to carry a sealed letter.",
             "You have broken several of the Guild's blades, and Ser Ugo's steward is polite now. He "
             "asks you to carry a sealed letter."},
            {{"Carry the letter for Albizzi coin",
              "Ser Ugo's coin is in your purse, and his men nod to you in the street.",
              150, 50, 0, 0, 0, 0, 0, -1, 0, {0, 0, 25, -10, 0}},
             {"Show the letter to the Signoria",
              "The Signoria's secretary thanks you. Ser Ugo's blades are not so grateful.",
              100, 50, 0, 0, 0, 0, 0, 1, 2, {0, 0, -10, 20, 0}}}},
        {   // day 50: the feast
            {"The Guild has bought half the palace guard. At the feast of San Giovanni their blades will "
             "strike the Gonfaloniere, and Ser Ugo knows you know.",
             "The Guild has friends in the palace guard. At the feast of San Giovanni their blades may "
             "strike the Gonfaloniere, and Ser Ugo knows you know.",
             "The Guild is thinner than it was, but its blades still mean to strike the Gonfaloniere at "
             "the feast of San Giovanni. Ser Ugo knows you know."},
            {{"Guard the Gonfaloniere's table", "Blades come out at the feast. The Gonfaloniere lives, and you bleed.",
              300, 200, -4, 0, 0, 0, 0, 1, 0, {0, 10, 20, 0, 0}},
             {"Sell the Guild the guard roster", "The Guild pays well for the roster. The Gonfaloniere dies at the feast.",
              500, 100, 0, 0, 0, 0, 0, -1, 0, {20, -10, 0, 20, 0}}}},
        {   // day 100: dawn in the piazza
            {"Dawn at the Piazza della Signoria: the Guild holds every doorway, and Ser Ugo's banner is "
             "on the Palazzo steps. Both camps want your voice.",
             "Dawn at the Piazza della Signoria: Ser Ugo's banner and the Signoria's face each other "
             "across the stones. Both camps want your voice.",
             "Dawn at the Piazza della Signoria: the Guild is thin, but Ser Ugo's banner still faces the "
             "Signoria's across the stones. Both camps want your voice."},
            {{"Stand with the Signoria", "You stand with the Signoria, and the piazza hears you.",
              200, 200, 0, 0, 0, 0, 2, 0, 0, {-10, 10, 0, 0, 0}},
             {"Stand with Ser Ugo", "You stand with Ser Ugo, and his coin follows.",
              400, 300, 0, 0, 0, 0, 0, 0, 0, {10, 0, 30, 0, 0}}}}}};

// What the player did at the previous beat: RECAP[plot][beat just played][choice]
static const char *RECAP[N_PLOTS][2][2] = {
    {{"The farmers you fed march behind Mona Tessa.",
      "The farmers you betrayed follow a harder leader than Mona Tessa."},
     {"The gate stood open, and the commons hold the Piazza.",
      "The gate held, and the farmers' camps still smoulder. The city is afraid."}},
    {{"The burned houses of Santa Croce still smoulder.",
      "The Maestro's cure sits on every apothecary's shelf."},
     {"His cellar lies in ruins, but his working goes on elsewhere.",
      "The wells of San Niccolo went bad, and the dying fill the hospices."}},
    {{"Ser Ugo's coin is in your purse, and his men nod to you.",
      "The Signoria's secretary trusts you now, and Ser Ugo's men do not."},
     {"The Gonfaloniere lives, and the Guild has your name.",
      "The Gonfaloniere is dead, and the Signoria sits leaderless."}}};

// The ending: ENDING[plot][day-100 choice] + ENDING_TAIL[tier at day 100]
static const char *ENDING[N_PLOTS][2] = {
    {"The Signoria grants the farmers their grain and their courts, and the brigands scatter into the hills.",
     "The Signoria breaks the march and hangs its leaders, and the brigands vanish into the hills."},
    {"The circle breaks, the Maestro flees over the Alps, and the fever dies with his working.",
     "The Maestro leaves a gift and a debt behind him. The fever fades, but not everywhere."},
    {"The Signoria's guards seize the Guild's blades at dawn, and Ser Ugo flees the city.",
     "Ser Ugo takes the Palazzo at dawn, and the Guild's blades hold its doors."}};
static const char *ENDING_TAIL[3] = {"The city paid dearly for the long delay.",
                                     "The city comes through scarred but standing.",
                                     "The city prospers, and the streets remember your blade."};

// Success rate against the active plot's own NPCs -> which version of a beat plays.
static int plotTier(const Player &P) {
    int n = P.plotWon + P.plotLost;
    if (n < 2) return 0;                    // barely opposed it: the plot advances unchecked
    int rate = P.plotWon * 100 / n;
    return rate < 35 ? 0 : rate > 65 ? 2 : 1;
}

// The city's mood.  Choice m at beat m shapes the act that follows (days BEAT_DAY[m] up to the next
// beat); the day-100 choice colours the endgame.  Read from the saved story fields, so no new state.
static Eco climate(const Player &P) {
    Eco c = {0, 0, 0, 0, 0};
    if (P.plot < 0 || P.plot >= N_PLOTS) return c;
    for (int m = 0; m < 3; m++) {
        if (P.plotChoice[m] < 1 || P.plotChoice[m] > 2) continue;
        bool live = P.plotStage == m + 1 || (m == 2 && P.plotStage >= 3);
        if (!live) continue;
        const Eco &e = BEAT[P.plot][m].opt[P.plotChoice[m] - 1].eco;
        c.upkeep += e.upkeep; c.wage += e.wage; c.patron += e.patron; c.loot += e.loot; c.herb += e.herb;
    }
    return c;
}
static string ecoText(const Eco &e) {
    string s;
    auto add = [&](int v, const char *what) {
        if (!v) return;
        if (!s.empty()) s += ", ";
        s += string(what) + (v > 0 ? " +" : " ") + to_string(v) + "%";
    };
    add(e.upkeep, "cost of living"); add(e.wage, "wages"); add(e.patron, "patron pay");
    add(e.loot, "bounties"); add(e.herb, "herbs found");
    return s;
}

// Cost of living per day: climbs with the days and with the florins you hold, shifts with the story.
static int upkeepCost(const Player &P) {
    int u = UPKEEP_BASE + (min(max(1, P.day), 100) - 1) / 10 * UPKEEP_RISE + P.df / UPKEEP_WEALTH;   // the rise stops at day 100
    return max(10, pct(u, climate(P).upkeep));
}
// What a legacy costs: the ending moves it once the day-100 beat has set the city's fate.
static int legacyCost(const Player &P) {
    if (P.plotStage < 3) return LEGACY_DF;
    int s = P.plotEndTier == 0 ? LEGACY_SPREAD : P.plotEndTier == 2 ? -LEGACY_SPREAD : 0;
    return pct(LEGACY_DF, s);
}
// Ritual fees: Scholars pay a quarter less.
static int ritualCost(const Player &P, int i) { return RC[i] * (P.cls == 2 ? 75 : 100) / 100; }
static int craftCost(const Player &P) { return CRAFT_MAT[min(max(P.crafted, 0), 3)]; }   // materials for one craft
static int spellCost(const Player &P) { return P.cls == 2 ? 2 : 3; }   // SP per cast: Scholars cast cheaply
static void startingKit(Player &P, int c) {
    P = Player();
    P.cls = c; P.p = START_P[c]; P.df = START_DF[c];
    if (c == 1) P.weapon = 2;    // Knight starts with a Longsword
    if (c == 2) P.spell[0] = 1;  // Scholar starts with Firebolt
    P.hp = P.maxhp(); P.sp = P.maxsp();
}
// Insight sells to patrons; the learned sell dearer.  Tenths of a florin.
static int insightValue(const Player &P) { return inc(P.insight * (12 + 5 * P.lore) / 100); }

static string plotEnding(const Player &P) {
    if (P.plot < 0 || !P.plotChoice[2]) return "";
    return string(ENDING[P.plot][P.plotChoice[2] - 1]) + " " + ENDING_TAIL[P.plotEndTier];
}

// ----------------------------------------------------------------------------
//  Game-over / victory
// ----------------------------------------------------------------------------
[[noreturn]] static void gameOver(Player &P, const string &cause) {
    clearScr();
    top();
    row();
    row("        *  *  *   Y O U   H A V E   D I E D   *  *  *");
    row();
    mid();
    rows(cause);
    row();
    rows(P.name + " the " + string(CLS[P.cls]) + " lasted " + to_string(P.day) +
         " days, holding " + to_string(P.p) + "p and " + fmtF(P.df) + "f.");
    row("DEATH IS PERMANENT. THE LEDGER IS CLOSED.");
    bot();
    remove(savePath(P.name, P.saveSlot).c_str());
    exit(0);
}

static void newGame(Player &P);

static void victory(Player &P) {
    static const char *reward[3] = {
        "You buy a GUILD HALL. Mass production: two items per hand.",
        "You buy a RURAL ESTATE. Tenants pay 50p a day, forever.",
        "You raise a PRIVATE LIBRARY. Insight comes twice as fast."};
    clearScr();
    top();
    row();
    row("      *  *  *   L E G A C Y   C L A I M E D   *  *  *");
    row();
    mid();
    rows(reward[P.cls]);
    row();
    {
        string end = plotEnding(P);
        if (!end.empty()) { rows(end); row(); }
    }
    rows(P.name + " the " + string(CLS[P.cls]) + " claimed a legacy in " +
         to_string(P.day) + " days.");
    row("IN FIORENCIA, ART IS WAR, AND YOU HAVE WON.");
    bot();
    if (ask("[1] START A NEW GAME   [2] QUIT: ", "12") == '2') exit(0);
    newGame(P);
}

// ----------------------------------------------------------------------------
//  Combat
// ----------------------------------------------------------------------------
struct Enemy {
    string name;
    int hp, maxhp, def, atk, sides, bonus, rewardP, rewardDf;
    int plot;  // story this NPC belongs to (-1 = flavour only). No default member initialiser:
               // that would stop Enemy being a brace-initialisable aggregate in C++11.
};
enum Result { WIN, FLED, KO, DRAW };
static int lootPct(const Player &P, const Enemy &E);   // defined with the story hooks below

static Enemy makeEnemy(bool duel = false) {
    if (duel) return {"Hot-Blooded Duelist", 16, 16, 12, 4, 6, 2, 80, 5, PLOT_ALBIZZI};
    switch (d(5)) {
        case 1: return {"Road Brigand", 14, 14, 12, 3, 6, 1, 60, 4, PLOT_REVOLT};
        case 2: return {"Plague-Mad Wolf", 12, 12, 11, 4, 6, 0, 0, 3, PLOT_SORCERER};
        case 3: return {"Condottiere Deserter", 18, 18, 13, 4, 8, 0, 100, 6, PLOT_REVOLT};
        case 4: return {"Pestilent Hound", 13, 13, 11, 4, 6, 1, 0, 3, PLOT_SORCERER};
        default: return {"Cutthroat of the Oltrarno", 15, 15, 12, 4, 6, 1, 50, 5, PLOT_ALBIZZI};
    }
}

static int defense(const Player &P, int stance, bool stone, bool shield) {
    int v = 10 + (P.body + 1) / 2 + (P.armor == 1 ? 1 : P.armor == 2 ? 3 : 0);
    if (P.cls == 1 && P.armor == 2) v += 2;  // Knight heavy-armor edge
    if (stone) v += 3;
    if (shield) v += 3;
    if (stance == 1) v -= 2;
    if (stance == 3) v += 2;
    return v;
}

static void loseItems(Player &P, int n) {
    // Slot ids: 0-4 potion, 10 herb, 11 ration, 20 weapon, 21 armor
    vector<int> slots;
    for (int i = 0; i < 5; i++)
        for (int k = 0; k < P.potion[i]; k++) slots.push_back(i);
    for (int k = 0; k < P.herbs; k++) slots.push_back(10);
    for (int k = 0; k < P.rations; k++) slots.push_back(11);
    if (P.weapon) slots.push_back(20);
    if (P.armor) slots.push_back(21);
    for (int i = 0; i < n; i++) {
        if (slots.empty()) {  // nothing left to take: they take florins
            int take = min(P.df, 1);
            P.df -= take;
            if (take) say("Thieves take 0.1f from your purse.");
            continue;
        }
        int idx = d((int)slots.size()) - 1;
        int s = slots[idx];
        slots.erase(slots.begin() + idx);
        if (s < 5) { P.potion[s]--; say(string("Lost: ") + POT[s]); }
        else if (s == 10) { P.herbs--; say("Lost: a bundle of herbs"); }
        else if (s == 11) { P.rations--; say("Lost: a ration"); }
        else if (s == 20) { say(string("Lost: your ") + WPN[P.weapon]); P.weapon = 0; P.ench = 0; }
        else { say(string("Lost: your ") + ARM[P.armor]); P.armor = 0; }
    }
}

static void drawCombat(const Player &P, const Enemy &E, int round, int maxRounds,
                       const vector<string> &cl, bool stone) {
    clearScr();
    string rr = maxRounds > 0 ? to_string(round) + " / " + to_string(maxRounds) : to_string(round);
    title("COMBAT  -  ROUND " + rr);
    row("YOU    HP " + bar(P.hp, P.maxhp(), 12) + " " + to_string(P.hp) + "/" +
        to_string(P.maxhp()) + "   SP " + to_string(P.sp));
    row("       DEF " + to_string(defense(P, 2, stone, false)) + " (no stance/ward)  " + WPN[P.weapon] +
        (P.ench ? " +" + to_string(P.ench) : string()) + (P.oil ? "  [FIRE OIL x" + to_string(P.oil) + "]" : ""));
    row();
    row(E.name);
    row("       HP " + bar(E.hp, E.maxhp, 12) + " " + to_string(E.hp) + "/" +
        to_string(E.maxhp) + "   DEF " + to_string(E.def));
    mid();
    for (auto &l : cl) rows(l);
    if (cl.empty()) row("Steel is drawn.");
    mid();
    row("STANCE:  [1] AGGRESSIVE (+2 dmg/-2 DEF)  [2] NORMAL");
    row("         [3] DEFENSIVE (+2 DEF/-2 dmg)");
    row("ACTION:  [S] STRIKE  [C] CAST  [U] USE  [F] FLEE");
    bot();
}

// maxRounds <= 0 means fight to the finish.
static Result combat(Player &P, Enemy E, int maxRounds, bool canFlee) {
    bool stone = P.stoneskin > 0;  // Stoneskin lasts for one fight
    P.stoneskin = 0;
    vector<string> cl;
    cl.push_back("A " + E.name + " bars your way!");
    for (int r = 1;; r++) {
        if (maxRounds > 0 && r > maxRounds) {
            clearScr();
            title("COMBAT  -  STALEMATE");
            rows("The " + E.name + " breaks off and slinks into the alleys.");
            bot();
            pause();
            return DRAW;
        }
        drawCombat(P, E, r, maxRounds, cl, stone);
        cl.clear();
        int stance = ask("STANCE [1-3]: ", "123") - '0';
        int sdmg = stance == 1 ? 2 : stance == 3 ? -2 : 0;
        bool shieldUp = false;
        bool acted = false;
        while (!acted) {
            char a = ask("ACTION [S/C/U/F]: ", "scuf");
            if (a == 's') {
                int atk = P.body + P.mundMod() + P.crafted;
                int roll = d(20);
                bool hit = roll == 20 || (roll != 1 && roll + atk >= E.def);
                if (hit) {
                    int dmg = d(WPN_DIE[P.weapon]) + P.body + P.mundMod() + sdmg + P.ench + P.crafted;
                    if (P.oil > 0) { dmg += 2; P.oil--; }
                    dmg = max(1, dmg);
                    E.hp -= dmg;
                    cl.push_back("You strike (" + to_string(roll) + "+" + to_string(atk) +
                                 ") for " + to_string(dmg) + " damage.");
                } else
                    cl.push_back("You swing and miss (" + to_string(roll) + "+" + to_string(atk) + ").");
                acted = true;
            } else if (a == 'c') {
                bool any = P.spell[0] || P.spell[1] || P.spell[2];
                if (!any) { out("!! YOU KNOW NO SPELLS.\n"); continue; }
                if (P.sp < spellCost(P)) { out("!! NOT ENOUGH SP (" + to_string(spellCost(P)) + " REQUIRED).\n"); continue; }
                string opts;
                string label;
                if (P.spell[0]) { opts += "f"; label += "[F]IREBOLT "; }
                if (P.spell[1]) { opts += "h"; label += "[H]EAL "; }
                if (P.spell[2]) { opts += "s"; label += "[S]HIELD "; }
                char sp = ask("CAST " + label + "> ", opts);
                P.sp -= spellCost(P);
                if (sp == 'f') {
                    int atk = P.lore + P.magMod() + (P.cls == 2 && P.armor == 2 ? -2 : 0);
                    int roll = d(20);
                    bool hit = roll == 20 || (roll != 1 && roll + atk >= E.def);
                    if (hit) {
                        int dmg = max(1, d(6) + d(6) + P.lore + sdmg);
                        E.hp -= dmg;
                        cl.push_back("Firebolt (" + to_string(roll) + "+" + to_string(atk) +
                                     ") scorches it for " + to_string(dmg) + ".");
                    } else
                        cl.push_back("Your firebolt goes wide (" + to_string(roll) + "+" + to_string(atk) + ").");
                } else if (sp == 'h') {
                    int h = d(6) + 2;
                    P.hp = min(P.maxhp(), P.hp + h);
                    cl.push_back("Heal restores " + to_string(h) + " HP.");
                } else {
                    shieldUp = true;
                    cl.push_back("A shimmering ward rises (+3 DEF).");
                }
                acted = true;
            } else if (a == 'u') {
                if (P.potion[1] == 0 && P.potion[2] == 0) { out("!! NOTHING USABLE (FIRE OIL / STONESKIN).\n"); continue; }
                char u = ask("USE [F]IRE OIL  [S]TONESKIN > ", "fs");
                if (u == 'f') {
                    if (!P.potion[1]) { out("!! NO FIRE OIL.\n"); continue; }
                    P.potion[1]--; P.oil = 3;
                    cl.push_back("You slick your blade: next 3 attacks +2 fire.");
                } else {
                    if (!P.potion[2]) { out("!! NO STONESKIN.\n"); continue; }
                    P.potion[2]--; stone = true;
                    cl.push_back("Your skin hardens like quarry stone (+3 DEF).");
                }
                acted = true;
            } else {  // flee
                if (!canFlee) { out("!! THERE IS NO FLEEING THIS.\n"); continue; }
                int roll = d(20) + P.body;
                if (roll >= 12) {
                    clearScr();
                    title("COMBAT  -  ESCAPED");
                    row("You bolt through the alleys and lose your pursuer.");
                    bot();
                    pause();
                    return FLED;
                }
                cl.push_back("You try to flee but are cut off (" + to_string(roll) + ").");
                acted = true;
            }
        }

        if (E.hp <= 0) {
            clearScr();
            title("COMBAT  -  VICTORY");
            rows("The " + E.name + " falls.");
            int lp = lootPct(P, E);
            int lootP = inc(E.rewardP) * lp / 100, lootF = inc(E.rewardDf) * lp / 100;
            if (lootP) { P.p += lootP; rows("Loot: " + to_string(lootP) + "p"); }
            if (lootF) { P.df += lootF; rows("Loot: " + fmtF(lootF) + "f"); }
            bot();
            pause();
            return WIN;
        }

        // Enemy turn
int pdef = defense(P, stance, stone, shieldUp);

{
    string note = "Your DEF this round: " + to_string(pdef) +
                  " (base " + to_string(defense(P, 2, false, false));
    if (stone)       note += ", +3 stoneskin";
    if (shieldUp)    note += ", +3 ward";
    if (stance == 1) note += ", -2 aggressive";
    if (stance == 3) note += ", +2 defensive";
    note += ").";
    cl.push_back(note);
}

int enemyRoll = d(20);
bool ehit = enemyRoll == 20 || (enemyRoll != 1 && enemyRoll + E.atk >= pdef);
if (ehit) {
    int dmg = max(1, d(E.sides) + E.bonus);
    P.hp -= dmg;
    cl.push_back("The " + E.name + " hits (" + to_string(enemyRoll) + "+" + to_string(E.atk) +
                 " vs DEF " + to_string(pdef) + ") for " + to_string(dmg) + ".");
} else
    cl.push_back("The " + E.name + " misses (" + to_string(enemyRoll) + "+" + to_string(E.atk) +
                 " vs DEF " + to_string(pdef) + ").");

        if (P.hp <= 0) {
            P.hp = 1;
            clearScr();
            title("COMBAT  -  UNCONSCIOUS");
            row("Darkness. You wake in a gutter, battered and robbed.");
            bot();
            int n = d(4);
            loseItems(P, n);
            pause();
            return KO;
        }
    }
}

// ----------------------------------------------------------------------------
//  Story hooks that need combat and items
// ----------------------------------------------------------------------------
static bool isPlotFoe(const Player &P, const Enemy &e) { return e.plot >= 0 && e.plot == P.plot; }

// What a defeated foe is worth: Knights take contracts and bounties, the active plot's foes carry
// posted bounties while the story is live, and the city's mood moves them.
static int lootPct(const Player &P, const Enemy &E) {
    int pc = 100;
    if (P.cls == 1) pc += 10;
    if (isPlotFoe(P, E) && P.plotStage < 3) pc += 25;
    pc += climate(P).loot;
    return max(25, pc);
}

// The plot hardens as the acts pass, and by how the player treated it at each beat.
static void plotScale(const Player &P, Enemy &e) {
    if (!isPlotFoe(P, e)) return;
    int s = P.plotStage;
    e.hp += 2 * s; e.maxhp += 2 * s; e.atk += s / 2;
    for (int m = 0; m < 3; m++) {
        if (!P.plotChoice[m]) continue;
        const Opt &o = BEAT[P.plot][m].opt[P.plotChoice[m] - 1];
        e.atk += o.eAtk; e.hp += o.eHp; e.maxhp += o.eHp;
    }
    e.atk = max(1, e.atk);
    e.hp = max(4, e.hp);
    e.maxhp = max(e.hp, e.maxhp);
}

// Every fight goes through here. Only the active plot's NPCs touch the story record.
static Result fight(Player &P, Enemy e, int maxRounds, bool canFlee) {
    plotScale(P, e);
    bool foe = isPlotFoe(P, e);
    Result r = combat(P, e, maxRounds, canFlee);
    if (foe) {
        if (r == WIN) P.plotWon++;
        else if (r == KO) P.plotLost++;   // FLED / DRAW change nothing
    }
    return r;
}

static void plotApply(Player &P, const Opt &o, int tier, int m) {
    string res = o.result;
    vector<string> fx;
    int df = o.df0 + o.dfT * tier;
    if (df < 0) { int pay = min(P.df, -df); P.df -= pay; fx.push_back("-" + fmtF(pay) + "f"); }
    else if (df > 0) { P.df += df; fx.push_back("+" + fmtF(df) + "f"); }
    if (o.hp < 0) { P.hp = max(1, P.hp + o.hp); fx.push_back(to_string(o.hp) + " HP"); }
    else if (o.hp > 0) { P.hp = min(P.maxhp(), P.hp + o.hp); fx.push_back("+" + to_string(o.hp) + " HP"); }
    if (o.rations) { P.rations += o.rations; fx.push_back("+" + to_string(o.rations) + " rations"); }
    if (o.herbs) { P.herbs += o.herbs; fx.push_back("+" + to_string(o.herbs) + " herbs"); }
    if (o.stat == 1 && P.body < 10) { P.body++; P.hp++; fx.push_back("BODY +1"); }
    else if (o.stat == 2 && P.wits < 10) { P.wits++; fx.push_back("WITS +1"); }
    else if (o.stat == 3 && P.lore < 10) { P.lore++; fx.push_back("LORE +1"); }
    string et = ecoText(o.eco);
    if (!et.empty()) fx.push_back(et + (m < 2 ? ", until the next act" : ", to the end of the tale"));
    if (!fx.empty()) {
        res += " (";
        for (size_t i = 0; i < fx.size(); i++) res += (i ? ", " : "") + fx[i];
        res += ")";
    }
    say(res);
    if (o.lose) loseItems(P, o.lose);
}

// Plays the next milestone beat once its day has come. Called at nightfall.
static void plotBeat(Player &P) {
    int m = P.plotStage;
    if (P.plot < 0 || m >= 3 || P.day < BEAT_DAY[m]) return;
    int tier = plotTier(P);
    const Beat &B = BEAT[P.plot][m];
    clearScr();
    title(string(PLOT_NAME[P.plot]) + "  -  DAY " + pad3(P.day));
    if (m > 0 && P.plotChoice[m - 1]) { rows(RECAP[P.plot][m - 1][P.plotChoice[m - 1] - 1]); row(); }
    rows(B.scene[tier]);
    row();
    row(string("[1] ") + B.opt[0].label);
    row(string("[2] ") + B.opt[1].label);
    bot();
    int c = ask("YOUR WORD [1-2]: ", "12") - '0';
    P.plotChoice[m] = c;
    P.plotStage = m + 1;
    if (m == 2) P.plotEndTier = tier;
    plotApply(P, B.opt[c - 1], tier, m);
    if (m == 2) {
        clearScr();
        title(string(PLOT_NAME[P.plot]) + "  -  THE END OF THE TALE");
        rows(plotEnding(P));
        row();
        rows(P.df >= legacyCost(P)
                 ? string("Your purse is full enough. CLAIM YOUR LEGACY when you are ready.")
                 : "CLAIM YOUR LEGACY once your purse holds " + fmtF(legacyCost(P)) + "f.");
        bot();
        pause();
    }
    saveGame(P);
}

// ----------------------------------------------------------------------------
//  Day-to-day commands
// ----------------------------------------------------------------------------
static bool needAP(Player &P, int n) {
    if (P.ap < n) {
        say("!! NOT ENOUGH ACTION POINTS (" + to_string(n) + " NEEDED).");
        return false;
    }
    return true;
}

static void doWork(Player &P) {
    if (!needAP(P, 1)) return;
    P.ap--;
    P.worked = 1;
    // NOTE: all income multipliers (familiar, craft rank, class bonus, insight, climate.wage) are folded into the single reported number on purpose. Do not decompose.
    int roll = d(20);
    int earn = wage(roll * 10);
    bool bonus = roll <= P.primary();
    if (bonus) earn = earn * 3 / 2;
    if (P.familiar) earn = earn * 6 / 5;                        // the +20% the Familiar ritual always promised
    if (P.cls == 0) earn += earn * 5 * P.crafted / 100;         // Artisan: each craft rank lifts every trade 5%
    if (P.cls == 1) earn = earn * (75 + 5 * P.armor) / 100;     // Knight: patrol pay is thin, better-armed men earn more
    if (P.cls == 2 && P.insight > 0) earn = earn * 5 / 4;       // Scholar: a morning's study sells the tutoring
    earn = pct(earn, climate(P).wage);
    P.p += earn;
    static const char *verb[3] = {"You craft and sell wares", "You patrol the wards for hire",
                                  "You copy manuscripts and tutor merchants' sons"};
    say(string(verb[P.cls]) + ": +" + to_string(earn) + "p (rolled " + to_string(roll) +
        (bonus ? ", skilled x1.5)" : ")"));
    // Knight patrols can turn into a scrap -- risk for reward
    if (P.cls == 1 && d(10) <= 3) {
        Enemy e = makeEnemy();
        say("PATROL: a " + e.name + " waylays you!");
        Result r = fight(P, e, 4, true);
        if (r == WIN) say("You drive it off and take its purse.");
        else if (r == KO) say("You wake in the gutter, lighter of purse.");
    }
}

static void doRest(Player &P) {
    if (!needAP(P, 1)) return;
    P.ap--;

    int hpRoll = d(3) + (P.cls == 1 ? 2 : 0);   // Knights are drilled to shake off a beating
    int spRoll = d(3);

    int hpBefore = P.hp;
    int spBefore = P.sp;

    P.hp = min(P.maxhp(), P.hp + hpRoll);
    P.sp = min(P.maxsp(), P.sp + spRoll);

    say("You rest. HP +" + to_string(P.hp - hpBefore) +
        " (rolled " + to_string(hpRoll) + "). SP +" +
        to_string(P.sp - spBefore) +
        " (rolled 1d3: " + to_string(spRoll) + ").");
}

static void doStudy(Player &P) {
    if (!needAP(P, 1)) return;
    P.ap--;
    int roll = d(20) + P.lore + (P.familiar ? 4 : 0) + (P.cls == 2 ? 3 : 0);   // Scholars study deepest
    P.insight += roll;
    say("You study the ancients: +" + to_string(roll) + " insight.");
}

static void doExchange(Player &P, string a) {
    a.erase(remove(a.begin(), a.end(), ' '), a.end());
    if (a.empty()) {
        outf("AMOUNT (e.g. 240p to buy florins, 2f to sell): ");
        a = lower(trim(readLine()));
        a.erase(remove(a.begin(), a.end(), ' '), a.end());
    }
    if (a.size() < 2 || (a.back() != 'p' && a.back() != 'f')) {
        say("!! USAGE: exchange 240p   or   exchange 1.5f");
        return;
    }
    char u = a.back();
    double v = atof(a.substr(0, a.size() - 1).c_str());
    if (u == 'p') {
        int amt = (int)v;
        if (amt < 10) { say("!! MINIMUM EXCHANGE IS 10p."); return; }
        if (amt > P.p) { say("!! INSUFFICIENT PICCIOLI."); return; }
        int df = amt * 10 / P.rate;
        if (df < 1) { say("!! TOO SMALL: THAT BUYS LESS THAN 0.1f."); return; }
        int cost = (df * P.rate + 9) / 10;
        P.p -= cost;
        P.df += df;
        say("Exchanged " + to_string(cost) + "p for " + fmtF(df) + "f.");
    } else {
        int df = (int)lround(v * 10);
        if (df < 1) { say("!! MINIMUM EXCHANGE IS 0.1f."); return; }
        if (df > P.df) { say("!! INSUFFICIENT FLORINS."); return; }
        int gain = df * P.rate / 10;
        P.df -= df;
        P.p += gain;
        say("Exchanged " + fmtF(df) + "f for " + to_string(gain) + "p.");
    }
}

// Market price after the LORE discount.
static int shopPrice(const Player &P, const Shop &s) {
    int base = s.kind == 3 ? 5 * upkeepCost(P) * 9 / 10 : s.cost;   // 5 days of food at a 10% bulk saving
    return base * (100 - P.loreDiscount()) / 100;
}

static void doBuy(Player &P, string arg) {
    if (arg.empty()) {
        clearScr();
        title("THE MARKET  -  ARMORY / GRIMOIRE / GOODS");
        row("ITEM                                    KEY          COST");
        row(rep("-", 58));
        for (int i = 0; i < NSHOP; i++) {
            const Shop &s = SHOP[i];
            int c = shopPrice(P, s);
            string cost = s.flor ? fmtF(c) + "f" : to_string(c) + "p";
            string l = s.name;
            l += string(max(1, 40 - (int)l.size()), ' ');
            string k = s.key;
            k += string(max(1, 13 - (int)k.size()), ' ');
            row(l + k + cost);
            if (s.kind == 1 && s.id == 2 && P.cls == 2) row("    [Scholars: -2 spell atk]");
        }
        row();
        row("LORE discount: " + to_string(P.loreDiscount()) + "% off all purchases.");
        row("Major assets cost FLORINS ONLY. Each purchase takes 1 AP.");
        row("You hold: " + to_string(P.p) + "p / " + fmtF(P.df) + "f");
        bot();
        outf("BUY WHAT? (blank to leave): ");
        arg = lower(trim(readLine()));
        if (arg.empty()) return;
    }
    const Shop *it = nullptr;
    for (int i = 0; i < NSHOP; i++)
        if (string(SHOP[i].key).find(arg) == 0) { it = &SHOP[i]; break; }
    if (!it) { say("!! NO SUCH ITEM: " + arg); return; }
    if (it->kind == 2 && P.spell[it->id]) { say("!! YOU ALREADY KNOW THAT SPELL."); return; }
    int cost = shopPrice(P, *it);
    if (it->flor ? P.df < cost : P.p < cost) {
        say(it->flor ? "!! INSUFFICIENT FLORINS." : "!! INSUFFICIENT PICCIOLI.");
        return;
    }
    if (!needAP(P, 1)) return;
    P.ap--;
    if (it->flor) P.df -= cost; else P.p -= cost;
    switch (it->kind) {
        case 0: P.weapon = it->id; P.ench = 0; say(string("Bought a ") + WPN[it->id] + "."); break;
        case 1: P.armor = it->id; say(string("Bought a ") + ARM[it->id] + "." +
                                      (it->id == 2 && P.cls == 2 ? string(" (Scholars in heavy armor: -2 to spell attacks.)") : string())); break;
        case 2: P.spell[it->id] = 1; say(string("Learned ") + SPL[it->id] + "."); break;
        case 3: P.rations += 5; say("Bought 5 days of rations."); break;
        case 4: P.herbs += 3; say("Bought 3 herbs."); break;
    }
}

static void doCraft(Player &P) {
    if (P.cls != 0) { say("!! ONLY ARTISANS CRAFT GEAR."); return; }
    // Materials per craft depend on your rank (CRAFT_MAT). When you can pay for the next rank's
    // tools in florins (CRAFT_TOOLS), this craft buys them: +1 ATK/DMG and better goods from now on.
    bool upgrade = P.crafted < 3 && P.df >= CRAFT_TOOLS[P.crafted];
    int matCost = craftCost(P);
    if (P.p < matCost) { say("!! CRAFTING NEEDS " + to_string(matCost) + "p OF MATERIALS."); return; }
    if (!needAP(P, 1)) return;
    P.ap--;
    P.p -= matCost;
    if (upgrade) {
        P.df -= CRAFT_TOOLS[P.crafted];
        P.crafted++;
        say("You buy finer tools: +1 ATK/DMG (x" + to_string(P.crafted) + "). -" +
            fmtF(CRAFT_TOOLS[P.crafted - 1]) + "f tools, -" + to_string(matCost) + "p materials.");
    } else if (P.crafted < 3) {
        say("You craft at rank " + to_string(P.crafted) + ". -" + to_string(matCost) + "p materials. " +
            "Next tools: " + fmtF(CRAFT_TOOLS[P.crafted]) + "f.");
    } else {
        say("Your workshop is at its peak. -" + to_string(matCost) + "p materials.");
    }
    // Class income route: sell the goods you make. Better rank, better goods.
    int roll = d(20) + P.wits;
    int earn = inc(roll * (CRAFT_K + CRAFT_STEP * P.crafted) / 10);
    if (P.familiar) earn = earn * 6 / 5;   // the Familiar minds the workshop: the same +20% it gives every trade
    P.p += earn;
    say("Sales of crafted goods: +" + to_string(earn) + "p (rolled " + to_string(roll) + ").");
}

static void doGather(Player &P) {
    if (!needAP(P, 1)) return;
    P.ap--;
    int roll = d(20) + P.wits + P.natMod() + (P.familiar ? 2 : 0);
    if (roll >= 10) {
        int n = max(1, pct(d(4), climate(P).herb));
        P.herbs += n;
        say("Gathering (" + to_string(roll) + "): found " + to_string(n) + " herbs.");
    } else
        say("Gathering (" + to_string(roll) + "): you find only weeds.");
}

static int findPotion(const string &a) {
    for (int i = 0; i < 5; i++)
        if (!a.empty() && string(POT_KEY[i]).find(a) == 0) return i;
    return -1;
}

static void doBrew(Player &P, const string &arg) {
    int i = findPotion(arg);
    if (i < 0) { say("!! BREW WHAT? healing/fireoil/stoneskin/nighteye/antidote"); return; }
    if (P.herbs < POT_HERBS[i]) { say("!! NEED " + to_string(POT_HERBS[i]) + " HERBS."); return; }
    if (P.p < 20) { say("!! BREWING COSTS 20p BASE."); return; }
    if (!needAP(P, 1)) return;
    P.ap--;
    P.p -= 20;
    P.herbs -= POT_HERBS[i];
    int roll = d(20) + P.wits + P.natMod() + (P.familiar ? 2 : 0);
    if (roll >= 10) {
        P.potion[i]++;
        say(string("Brewed ") + POT[i] + " (" + to_string(roll) + ").");
    } else
        say(string("The ") + POT[i] + " curdles (" + to_string(roll) + "). Ingredients wasted.");
}

static void doUse(Player &P, const string &arg) {
    int i = findPotion(arg);
    if (i < 0) { say("!! USE WHAT? healing/fireoil/stoneskin/nighteye/antidote"); return; }
    if (!P.potion[i]) { say(string("!! YOU HAVE NO ") + POT[i] + "."); return; }
    P.potion[i]--;
    switch (i) {
        case 0: {
            int h = d(6) + 2;
            int b = P.hp;
            P.hp = min(P.maxhp(), P.hp + h);
            say("Healing Salve: HP +" + to_string(P.hp - b) + ".");
            break;
        }
        case 1: P.oil = 3; say("Fire Oil on your blade: next 3 attacks +2 fire."); break;
        case 2: P.stoneskin = 1; say("Stoneskin ready: +3 DEF in your next fight."); break;
        case 3: P.nighteye = 1; say("Night-Eye: you see in the dark until day's end."); break;
        case 4:
            if (P.poisoned) say("Antidote: the poison is purged.");
            else say("Antidote drunk. You were not poisoned.");
            P.poisoned = 0;
            break;
    }
}

static void doPrepare(Player &P, const string &arg) {
    int i = -1;
    for (int k = 0; k < 7; k++)
        if (!arg.empty() && string(RK[k]).find(arg) == 0) { i = k; break; }
    if (i < 0) {
        say("!! PREPARE WHAT? weapon/tongues/familiar/ward/boon/loreboon/witsboon  (see MAGIC)");
        return;
    }
    bool bad = false;
    if (i == 0) bad = !P.weapon;
    else if (i == 1) bad = P.gift;
    else if (i == 2) bad = P.familiar;
    else if (i == 3) bad = P.ward;
    else if (i == 4) bad = P.body >= 10;
    else if (i == 5) bad = P.lore >= 10;
    else if (i == 6) bad = P.wits >= 10;
    if (bad) {
        if (i == 0) say("!! YOU HAVE NO WEAPON TO ENCHANT.");
        else if (i >= 4) say("!! THAT ATTRIBUTE IS ALREADY AT MAXIMUM.");
        else say("!! THAT BARGAIN IS ALREADY STRUCK.");
        return;
    }
    int fee = ritualCost(P, i);
    if (P.df < fee) { say("!! INSUFFICIENT FLORINS (" + fmtF(fee) + "f)."); return; }
    if (P.hp <= 1) { say("!! TOO WEAK TO GIVE BLOOD."); return; }
    if (!needAP(P, 1)) return;
    P.ap--;
    P.df -= fee;
    P.hp -= 1;
    P.prepared = i;
    say(string("Circle drawn for: ") + RN[i] + ". -" + fmtF(fee) + "f, -1 HP. Now INVOKE.");
}

static void doInvoke(Player &P) {
    if (P.prepared < 0) { say("!! NO RITUAL PREPARED."); return; }
    if (!needAP(P, 2)) return;
    P.ap -= 2;
    int i = P.prepared;
    P.prepared = -1;
    int failPct = P.cls == 1 ? 200 : P.cls == 2 ? 50 : 100;  // Knights suffer double failure damage, Scholars half
    int roll = d(20) + P.lore + P.ritMod();
    if (roll >= 12) {
        switch (i) {
            case 0: P.ench++; say("The blade burns bright. +1 damage, permanently."); break;
            case 1: P.gift = 1; say("Every tongue of the world opens to you. Patron quests pay half again."); break;
            case 2: P.familiar = 1; say("A familiar binds to you: +2 gather/brew, +20% work and craft income."); break;
            case 3: P.ward = 1; say("Your home is warded: thieves will take only half."); break;
            case 4: P.body++; P.hp++; say("Your body hardens. BODY is now " + to_string(P.body) + "."); break;
            case 5: P.lore++; say("Your mind expands. LORE is now " + to_string(P.lore) + "."); break;
            case 6: P.wits++; say("Your cunning sharpens. WITS is now " + to_string(P.wits) + "."); break;
        }
        say("(invocation roll " + to_string(roll) + ")");
        return;
    }
    say("THE RITUAL FAILS (" + to_string(roll) + ").");
    switch (i) {
        case 0: P.weapon = 0; P.ench = 0; say("The weapon crumbles to slag. Weapon lost."); break;
        case 1: {
            int l = min(max(1, failPct / 100), P.lore - 1);
            P.lore -= l;
            say("Migraines. LORE -" + to_string(l) + ".");
            break;
        }
        case 2: {
            say("A DEMON tears through the circle!");
            pause();
            Enemy dm{"Unbound Demon", 25, 25, 12, 5, 6, 2, 0, 0, -1};
            combat(P, dm, 0, false);
            break;
        }
        case 3: {
            int lose = min(P.df, (P.df * d(4) / 50 + 10) * failPct / 100);   // 2-8% of the purse, and a little more
            P.df -= lose;
            say("Spirits pick your pouch: -" + fmtF(lose) + "f.");
            break;
        }
        case 4:
        case 5:
        case 6: {
            int dmg = max(1, d(6) * failPct / 100);
            P.hp -= dmg;
            say("Backlash! You lose " + to_string(dmg) + " HP.");
            if (P.hp <= 0) gameOver(P, "The ritual's backlash stops your heart.");
            break;
        }
    }
}

static void doQuest(Player &P) {
    if (!needAP(P, 1)) return;
    P.ap--;
    Eco c = climate(P);
    int roll = d(8);
    // Knights are hired for dangerous errands: cutpurses and vipers rarely find them.
    if (P.cls == 1 && (roll == 4 || roll == 6) && d(2) == 1) {
        static const int hired[4] = {1, 2, 3, 7};
        roll = hired[d(4) - 1];
    }
    switch (roll) {
        case 1: {
            int df = pct(inc(25 + d(20)), c.patron);
            if (P.cls == 1) df = df * 23 / 20;   // a captain's contract pays a Knight 15% better
            if (P.gift) { df = df * 3 / 2; say("Your Gift of Tongues wins a richer patron."); }
            P.df += df;
            if (P.cls == 1) say("CONTRACT: a captain pays " + fmtF(df) + "f for a week of your sword.");
            else say("PATRON: a merchant-prince pays " + fmtF(df) + "f for a quiet errand.");
            break;
        }
        case 2:
        case 3: {
            Enemy e = makeEnemy();
            if (!isPlotFoe(P, e) && d(2) == 1) e = makeEnemy();  // soft lean toward the active plot; nobody is removed
            say("MONSTER: a " + e.name + " attacks!");
            Result r = fight(P, e, 3, true);
            say(r == WIN ? "You won the fight." : r == KO ? "You lost the fight." :
                r == FLED ? "You escaped." : "The fight went nowhere.");
            break;
        }
        case 4: {
            int sp = P.p / 4;
            int cut = min(100, max(5, P.df / 25));   // about 4% of the florins you hold: at least 0.5f, at most 10f
            if (P.ward) cut /= 2;
            int sf = min(P.df, cut);
            P.p -= sp;
            P.df -= sf;
            say("THEFT: cutpurses take " + to_string(sp) + "p" + (sf ? " and " + fmtF(sf) + "f" : string()) + ".");
            if (P.ward) say("Your ward halves what they carry off.");
            break;
        }
        case 5: {
            int w = inc(d(20) * 10);
            P.p += w;
            say("WINDFALL: you find a dropped purse. +" + to_string(w) + "p.");
            break;
        }
        case 6:
            if (d(20) + P.wits >= 13) say("HAZARD: a viper in the warehouse. You step clear.");
            else { P.poisoned = 1; say("HAZARD: bitten! POISONED: -1 HP per day until cured."); }
            break;
        case 7: {
            Enemy e = makeEnemy(true);
            say("DUEL: a duelist challenges you in the piazza!");
            Result r = fight(P, e, 3, true);
            say(r == WIN ? "You win the duel." : r == KO ? "You lost the duel." :
                r == FLED ? "You walked away." : "The duel ends unsettled.");
            break;
        }
        default:
            if (P.nighteye) { P.herbs++; say("OMEN: with Night-Eye you spot a herb patch in the dark. +1 herb."); }
            else {
                int tip = inc(d(6) * 10);
                P.p += tip;
                say("OMEN: " + lower(P.plot >= 0 ? string(PLOT_NAME[P.plot]) : string("the city")) +
                    " is on every tongue, and a grateful stranger tips you " + to_string(tip) + "p.");
            }
    }
}

// ----------------------------------------------------------------------------
//  Info screens
// ----------------------------------------------------------------------------
static void showStatus(const Player &P) {
    clearScr();
    title("CITIZEN DOSSIER");
    row("NAME: " + P.name + "    CLASS: " + CLS[P.cls]);
    row("DAY:  " + pad3(P.day) + "    WEEK: " + to_string((P.day - 1) / 7 + 1));
    row();
    row("BODY " + bar(P.body, 10, 10) + " " + to_string(P.body) + "/10  HP & carry weight");
    row("WITS " + bar(P.wits, 10, 10) + " " + to_string(P.wits) + "/10  craft / natural magic");
    row("LORE " + bar(P.lore, 10, 10) + " " + to_string(P.lore) + "/10  ritual magic / trade");
    row();
    row("HP " + bar(P.hp, P.maxhp(), 14) + " " + to_string(P.hp) + "/" + to_string(P.maxhp()));
    row("SP " + bar(P.sp, P.maxsp(), 14) + " " + to_string(P.sp) + "/" + to_string(P.maxsp()));
    row("DEF " + to_string(defense(P, 2, false, false)));
    row("ATK +" + to_string(P.body + P.mundMod() + P.crafted));
    row("DMG 1d" + to_string(WPN_DIE[P.weapon]) + " + " + to_string(P.body + P.mundMod() + P.ench + P.crafted));
    if (P.spell[0] || P.spell[1] || P.spell[2]) {
        int satk = P.lore + P.magMod() + (P.cls == 2 && P.armor == 2 ? -2 : 0);
        char sbuf[16];
        snprintf(sbuf, sizeof sbuf, "%+d", satk);
        row("SPELL ATK " + string(sbuf) + (P.cls == 2 && P.armor == 2 ? " (-2 heavy armor)" : ""));
    }
    if (P.spell[0]) row("FIREBOLT 2d6 + " + to_string(P.lore));
    row("Market discount: " + to_string(P.loreDiscount()) + "% (from LORE)");
    mid();
    row("Weapon: " + string(WPN[P.weapon]) + (P.ench ? " (+" + to_string(P.ench) + " enchant)" : ""));
    row("Armor:  " + string(ARM[P.armor]));
    string sp;
    for (int i = 0; i < 3; i++) if (P.spell[i]) sp += string(SPL[i]) + " ";
    row("Spells: " + (sp.empty() ? string("none") : sp));
    row("Herbs: " + to_string(P.herbs) + "   Rations: " + to_string(P.rations) +
        (P.cls == 0 ? "   Crafted gear: +" + to_string(P.crafted) : string()));
    row("Insight: " + to_string(P.insight) + "  (converts to florins at nightfall)");
    string pt;
    for (int i = 0; i < 5; i++) if (P.potion[i]) pt += string(POT[i]) + " x" + to_string(P.potion[i]) + "  ";
    rows("Potions: " + (pt.empty() ? string("none") : pt));
    string bn;
    if (P.familiar) bn += "Familiar  ";
    if (P.gift) bn += "Tongues  ";
    if (P.ward) bn += "Ward  ";
    if (P.poisoned) bn += "[POISONED]  ";
    if (P.prepared >= 0) bn += string("[Prepared: ") + RN[P.prepared] + "]";
    row("Pacts/Status: " + (bn.empty() ? string("none") : bn));
    if (P.plotStage > 0) row(string("Story: ") + PLOT_NAME[P.plot] + "  (act " + to_string(P.plotStage) + "/3)");
    bot();
    pause();
}

static void showMagic(const Player &P) {
    clearScr();
    title("MAGIC SYSTEM");
    row("NATURAL MAGIC (Alchemy)  - temporary, costs p, key WITS");
    row("  gather            find 1d4 herbs               (1 AP)");
    row("  brew <potion>     20p + herbs, roll WITS>=10   (1 AP)");
    row("  use <potion>      drink / apply                (free)");
    row("    healing   2 herbs  1d6+2 HP, out of combat");
    row("    fireoil   1 herb   next 3 attacks +2 fire");
    row("    stoneskin 2 herbs  +3 DEF for one fight");
    row("    nighteye  1 herb   see in the dark for the day");
    row("    antidote  1 herb   cures poison");
    mid();
    row("RITUAL MAGIC (Pacts) - permanent, costs f + 1 HP, key LORE");
    row("  prepare <ritual>  pay florins + blood            (1 AP)");
    row("  invoke            roll d20+LORE+class >= 12      (2 AP)");
    row("  Ritual     Cost  Success            Failure");
    auto line = [&](int i, const char *succ, const char *fail) {
        string k = RK[i];
        k += string(max(1, 11 - (int)k.size()), ' ');
        string c = fmtF(ritualCost(P, i)) + "f";
        c += string(max(1, 6 - (int)c.size()), ' ');
        string s = succ;
        s += string(max(1, 19 - (int)s.size()), ' ');
        row("  " + k + c + s + fail);
    };
    line(0, "+1 dmg forever", "weapon lost");
    line(1, "patron pay x1.5", "-1 LORE");
    line(2, "+2 gather/brew,", "25HP demon NOW");
    row("                   +20% work & craft");
    line(3, "thieves take half", "lose ~5% of purse");
    line(4, "+1 BODY (max 10)", "lose 1d6 HP");
    line(5, "+1 LORE (max 10)", "lose 1d6 HP");
    line(6, "+1 WITS (max 10)", "lose 1d6 HP");
    row("  Knights: -2 to ritual rolls, double failure damage.");
    row("  Scholars: +2 to rolls, fees -25%, half failure damage.");
    row("Note: Scholars wearing Brigandine suffer -2 to spell attacks.");
    bot();
    pause();
}

static void showHelp(const Player &P) {
    clearScr();
    title("COMMANDS");
    row("work            1 AP  1d20 x" + to_string(wage(10)) + " p (x1.5 if roll <= primary)");
    row("rest            1 AP  recover 1d3 HP (Knights +2) and SP");
    row("study           1 AP  gain insight -> florins at nightfall");
    row("exchange <amt>  0 AP  e.g. 240p -> florins, 1.5f -> p");
    row("buy [item]      1 AP  p for goods, f for gear and spells");
    row("quest           1 AP  patron/monster/theft/windfall/...");
    row("craft           1 AP  (Artisan) sells goods; buys tools");
    row("                      for +1 ATK/DMG: " + fmtF(CRAFT_TOOLS[0]) + "/" + fmtF(CRAFT_TOOLS[1]) + "/" +
        fmtF(CRAFT_TOOLS[2]) + "f");
    row("gather / brew <potion> / use <potion>");
    row("prepare <ritual> / invoke        (see MAGIC)");
    row("status  magic  ledger  save  quit");
    row("end             end the day early");
    row("legacy          spend ~" + fmtF(LEGACY_DF) + "f (after day 100) to WIN");
    mid();
    row("Upkeep: " + to_string(upkeepCost(P)) + "p at day's end (rations count instead).");
    row("  It rises with the days, with your florins and with");
    row("  the city's mood. Rations are priced from it.");
    row("Can't pay: -1 HP. A day without WORK heals +1 HP.");
    row("Poison costs 1 HP a day. At 0 HP from hunger: death.");
    row("Knocked out in combat: wake with 1 HP, lose 1d4 items.");
    row("Exchange rate rerolls weekly: 1d10, even +, odd -.");
    row("Market prices cut by LORE: Scholars up to 15%, others 5%.");
    row("Shortcuts: 1=work 2=rest 3=exchange 4=buy 5=quest");
    bot();
    pause();
}

static void showLedger(const Player &P) {
    clearScr();
    title("THE LEDGER");
    row("PICCIOLI: " + to_string(P.p) + "p        FLORINS: " + fmtF(P.df) + "f");
    row("INSIGHT:  " + to_string(P.insight) + "  (-> " + fmtF(insightValue(P)) + "f at nightfall)");
    row("UPKEEP:   " + to_string(upkeepCost(P)) + "p a day, rising with the days and your purse");
    string et = ecoText(climate(P));
    if (!et.empty()) rows("CITY MOOD: " + et + ".");
    row();
    row("EXCHANGE RATE (this week):  1 f = " + to_string(P.rate) + " p");
    row("Weekly roll 1d10: EVEN -> rate + roll, ODD -> rate - roll");
    row("Value of your florins: " + to_string(P.df * P.rate / 10) + "p");
    row();
    row("GOAL: " + fmtF(legacyCost(P)) + " florins, then 'legacy' once day 100 has passed.");
    if (P.plotStage < 3) row("(How the story ends moves the price by 10%, up or down.)");
    row("LEGACY " + bar(P.df, legacyCost(P), 20) + " " + fmtF(P.df) + "/" + fmtF(legacyCost(P)) + "f");
    bot();
    pause();
}

// ----------------------------------------------------------------------------
//  Day cycle
// ----------------------------------------------------------------------------
static const char *FLAVOR[] = {
    "THE BELLS OF THE DUOMO TOLL.", "A BANKER'S SEAL IS FOUND BROKEN.",
    "SMOKE RISES FROM THE ARNO WORKSHOPS.", "WHISPERS OF A GUILD FEUD.",
    "FOG ROLLS DOWN FROM FIESOLE.", "THE CHAPEL CANDLES GUTTER.",
    "A MERCENARY COMPANY CAMPS BEYOND THE WALLS.", "THE PIAZZA SMELLS OF BREAD AND IRON."};

static void drawDay(Player &P) {
    clearScr();
    top();
    row("DAY " + pad3(P.day) + "  WEEK " + to_string((P.day - 1) / 7 + 1) +
        "   ACTION POINTS: " + rep("#", P.ap) + rep(".", 3 - P.ap) + " (" + to_string(P.ap) + "/3)");
    mid();
    row(P.name + " the " + CLS[P.cls] + (P.poisoned ? "   [POISONED]" : ""));
    row("HP " + bar(P.hp, P.maxhp(), 12) + " " + to_string(P.hp) + "/" + to_string(P.maxhp()) +
        "   SP " + bar(P.sp, P.maxsp(), 12) + " " + to_string(P.sp));
    row("PICCIOLI " + to_string(P.p) + "p   FLORINS " + fmtF(P.df) + "f   1f=" + to_string(P.rate) + "p");
    row("LEGACY " + bar(P.df, legacyCost(P), 20) + " " + fmtF(P.df) + "/" + fmtF(legacyCost(P)) + "f");
    row("UPKEEP " + to_string(upkeepCost(P)) + "p/day");
    {
        string mood = ecoText(climate(P));
        if (!mood.empty()) rows("CITY MOOD: " + mood);
    }
    row("BODY " + to_string(P.body) + "  WITS " + to_string(P.wits) + "  LORE " + to_string(P.lore) +
        "   Weapon: " + WPN[P.weapon]);
    if (P.insight) row("INSIGHT " + to_string(P.insight) + "  (-> " + fmtF(insightValue(P)) + "f tonight)");
    mid();
    if (g_log.empty()) row(string(FLAVOR[(P.day - 1) % 8]));
    for (auto &l : g_log) rows(l);
    mid();
    row("work rest study exchange buy quest | gather brew use | craft");
    row("prepare invoke | status magic ledger help | legacy end");
    bot();
}

static void endDay(Player &P) {
    vector<string> notes;

    // Insight converts to florins at nightfall (2 insight : 0.1f).
    if (P.insight > 0) {
        int conv = insightValue(P);
        P.df += conv;
        P.insight = 0;
        if (conv > 0) notes.push_back("Insight sold to patrons: +" + fmtF(conv) + "f.");
    }

    int up = upkeepCost(P);
    if (P.rations > 0) { P.rations--; notes.push_back("A ration is eaten. (" + to_string(P.rations) + " left)"); }
    else if (P.p >= up) { P.p -= up; notes.push_back("Upkeep paid: -" + to_string(up) + "p."); }
    else { P.hp--; notes.push_back("!! CANNOT PAY UPKEEP. STARVATION: -1 HP."); }
    if (P.hp <= 0) gameOver(P, "You starve in a cold Fiorentine garret.");
    if (P.poisoned) {
        P.hp--;
        notes.push_back("!! POISON: -1 HP.");
        if (P.hp <= 0) gameOver(P, "The viper's venom finishes its work.");
    }
    if (!P.worked && P.hp < P.maxhp()) { P.hp++; notes.push_back("A day of rest heals +1 HP."); }

    P.day++;
    if ((P.day - 1) % 7 == 0) {
        int r = d(10);
        int old = P.rate;
        P.rate = (r % 2 == 0) ? P.rate + r : P.rate - r;
        P.rate = max(40, min(300, P.rate));
        notes.push_back("NEW WEEK. Exchange roll " + to_string(r) + (r % 2 ? " (odd)" : " (even)") +
                        ": " + to_string(old) + " -> " + to_string(P.rate) + " p/f.");
    }
    if (P.plot >= 0 && P.day % 5 == 0)
        notes.push_back(string("NEWS: ") + RUMOR[P.plot][P.plotStage][(P.day / 5) % 2]);
    P.ap = 3;
    P.worked = 0;
    P.nighteye = 0;
    saveGame(P);

    clearScr();
    title("NIGHTFALL  -  DAY " + pad3(P.day - 1) + " ENDS");
    for (auto &n : notes) rows(n);
    row();
    row(string(FLAVOR[d(8) - 1]) + "  DAY " + pad3(P.day) + " BEGINS.");
    bot();
    pause();
    g_log.clear();
    plotBeat(P);
}

static void tryLegacy(Player &P) {
    int cost = legacyCost(P);
    if (P.df < cost) {
        say("!! A LEGACY COSTS " + fmtF(cost) + "f. YOU HOLD " + fmtF(P.df) + "f.");
        return;
    }
    if (P.plotStage < 3) {
        say("!! THE CITY'S FATE IS UNSETTLED. CLAIM YOUR LEGACY AFTER DAY 100.");
        return;
    }
    P.df -= cost;
    if (saveGame(P)) markLegacy(P);
    victory(P);
}


 static void playLoop(Player &P) {
    while (true) {
        if (P.ap == 0) { endDay(P); continue; }
        drawDay(P);
        string line = prompt();
        string cmd, arg;
        splitCmd(line, cmd, arg);
        if (cmd == "brew" && arg.empty()) say("Did you mean 'brew healing/fireoil/stoneskin/nighteye/antidote");
        else if (cmd == "1" || cmd == "work") doWork(P);
        else if (cmd == "2" || cmd == "rest") doRest(P);
        else if (cmd == "3" || cmd == "exchange" || cmd == "x") doExchange(P, arg);
        else if (cmd == "4" || cmd == "buy") doBuy(P, arg);
        else if (cmd == "5" || cmd == "quest") doQuest(P);
        else if (cmd == "craft") doCraft(P);
        else if (cmd == "study") doStudy(P);
        else if (cmd == "gather") doGather(P);
        else if (cmd == "brew") doBrew(P, arg);
        else if (cmd == "use") doUse(P, arg);
        else if (cmd == "prepare") doPrepare(P, arg);
        else if (cmd == "invoke") doInvoke(P);
        else if (cmd == "status" || cmd == "stats") showStatus(P);
        else if (cmd == "magic") showMagic(P);
        else if (cmd == "ledger") showLedger(P);
        else if (cmd == "help" || cmd == "?") showHelp(P);
        else if (cmd == "legacy") tryLegacy(P);
        else if (cmd == "end") { P.ap = 0; }
        else if (cmd == "save") { say(saveGame(P) ? "Game saved." : "!! COULD NOT WRITE SAVE FILE."); }
        else if (cmd == "quit" || cmd == "exit") {
            saveGame(P);
            out("SAVED. THE LEDGER WAITS.\n");
            exit(0);
        } else if (!cmd.empty()) say("!! UNKNOWN COMMAND: " + cmd + "  (try HELP)");
    }
}

// ----------------------------------------------------------------------------
//  Title / new game
// ----------------------------------------------------------------------------
static void bootScreen() {
    clearScr();
    top();
    row("F I O R E N C I A   T E R M I N A L   v1.0");
    row("(c) 1984 CASA DELLA LIRA - ALL RIGHTS RESERVED");
    mid();
    row();
    row("LOADING: DOMES.DAT  GUILDS.DAT  LEDGER.DAT  HERESY.SYS");
    row(rep("#", 40) + "  100%");
    row("...OK");
    row("...OK");
    row("...OK");
    row("...OK");
    row();
    row("PRESS ANY KEY TO BEGIN");
    bot();
    pause();
}

static void settingScreen() {
    clearScr();
    title("THE SETTING");
    rows("Welcome to FIORENCIA - a city of soaring domes, bloody guild feuds, and forbidden knowledge. Bank ledgers are as deadly as longswords. The path to power runs through workshops, chapels, and mercenary companies.");
    row();
    rows("You are not a chosen hero. You are a CITIZEN, clawing upward through investments, masterpieces, arcane pacts, and the occasional duel in the piazza.");
    row();
    row("TWO CURRENCIES. ONE CITY.");
    row("  PICCIOLI (p)  daily bread, rent, common goods");
    row("  FLORINS  (f)  dreams, workshops, rituals");
    row("  The exchange rate shifts weekly with war and famine.");
    row();
    row("GOAL: Amass about " + fmtF(LEGACY_DF) + " FLORINS, then claim your legacy.");
    rows("Something stirs in the city, and it will not wait for you. By day 100 its story will be told, with or without your hand in it.");
    row("In Fiorencia, ART IS WAR.");
    bot();
    pause();
}

// Character creation: the class's primary stat gets +1, then 3 points to place freely.
// (5 base + 1 + 3 tops out at 9, under the in-game cap of 10.)
static void assignStats(Player &P) {
    static const char *PRIM[3] = {"WITS", "BODY", "LORE"};
    if (P.cls == 0) P.wits++;
    else if (P.cls == 1) P.body++;
    else P.lore++;
    for (int left = 3; left > 0; left--) {
        clearScr();
        title("ASSIGN YOUR ATTRIBUTES");
        rows(string("Your ") + CLS[P.cls] + " training gave +1 " + PRIM[P.cls] + ". Place " +
             to_string(left) + " more point" + (left > 1 ? "s" : "") + " as you choose.");
        row();
        row("BODY " + bar(P.body, 10, 10) + " " + to_string(P.body) + "/10  HP & carry weight");
        row("WITS " + bar(P.wits, 10, 10) + " " + to_string(P.wits) + "/10  craft / natural magic");
        row("LORE " + bar(P.lore, 10, 10) + " " + to_string(P.lore) + "/10  ritual magic / trade");
        bot();
        char c = ask("ADD +1 TO [B]ODY, [W]ITS OR [L]ORE: ", "bwl");
        if (c == 'b') P.body++;
        else if (c == 'w') P.wits++;
        else P.lore++;
    }
}

static void newGame(Player &P) {
    settingScreen();
    clearScr();
    title("CHOOSE YOUR ESTATE");
    row("[1] ARTISAN  \"The city is my workshop.\"");
    row("    Primary WITS.  Start 550p / 3f.");
    row("    Edge: craft gear for +1 ATK/DMG (up to +3) AND sell the");
    row("          goods; tools cost florins, each rank lifts trade.");
    row();
    row("[2] KNIGHT   \"Steel settles ledgers.\"");
    row("    Primary BODY.  Start 250p / 4f + a free Longsword.");
    row("    Edge: +2 mundane ATK and +2 DMG; heavy armor +2 DEF.");
    row("          Contracts +15%, bounties +10%, rest heals +2 HP.");
    row("    Penalty: -2 magic ATK, -2 gather/brew rolls,");
    row("             -2 ritual rolls, DOUBLE ritual failure damage.");
    row();
    row("[3] SCHOLAR  \"Knowledge is the knife.\"");
    row("    Primary LORE.  Start 400p / 4f + Firebolt already known.");
    row("    Edge: +2 magic ATK and ritual rolls; rituals 25% off,");
    row("          spells cost 2 SP, study pays best, LORE cuts");
    row("          market prices by up to 15%.");
    row("    Penalty: -2 mundane ATK.");
    bot();
    int c = ask("SELECT CLASS [1-3]: ", "123") - '1';
    startingKit(P, c);
    assignStats(P);
    P.plot = g_forcePlot >= 0 ? g_forcePlot : d(N_PLOTS) - 1;
    P.day = g_startDay;
    outf("NAME YOUR CITIZEN: ");
    string n = trim(readLine());
    if (n.empty()) n = "Citizen";
    P.name = n.substr(0, 16);
    outf("SAVE SLOT FOR " + P.name + " [1-3] (existing slot will be overwritten): ");
    P.saveSlot = ask("", "123") - '0';
    P.hp = P.maxhp();
    P.sp = P.maxsp();
    g_log.clear();
    showStatus(P);
}

static void manual() {
    Player demo;
    settingScreen();
    showHelp(demo);
    showMagic(demo);
}

int main(int argc, char **argv) {
    // NOTE: this translation unit is compiled with -fno-exceptions -fno-rtti.
    // Do not add stoi/stol/stod, .at(), throw, dynamic_cast, or typeid below.
    // See BUILD NOTES at the top of the file  
	for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--amber") g_on = "\033[38;5;214m";
        else if (a == "--plain") { g_color = false; g_clear = false; }
        else if (a == "--seed" && i + 1 < argc) rng.seed((unsigned)atoi(argv[++i]));
        else if (a == "--plot" && i + 1 < argc) g_forcePlot = atoi(argv[++i]) % N_PLOTS;
        else if (a == "--day" && i + 1 < argc) g_startDay = max(1, atoi(argv[++i]));
    }
    if (g_color) out(g_on);
    atexit(resetTerm);

    bootScreen();
    Player P;
    while (true) {
        clearScr();
        bool hasSave = true;
        top();
        row();
        row("        F I O R E N C I A      A R T   I S   W A R");
        row();
        row("        DOME      TOWER     GUILD     CHAPEL");
        mid();
        row("[1]  NEW GAME");
        row(string("[2]  CONTINUE") + (hasSave ? "" : "            (no save found)"));
        row("[3]  READ MANUAL");
        row("[4]  QUIT TO DOS");
        bot();
        char c = ask("> ", "1234");
        if (c == '1') { newGame(P); playLoop(P); }
        else if (c == '2') {
            outf("PLAYER NAME: ");
            P.name = trim(readLine());
            outf("SAVE SLOT [1-3]: ");
            P.saveSlot = ask("", "123") - '0';
            if (!hasSave || !loadGame(P)) { out("!! NO VALID SAVE FOR THAT NAME/SLOT.\n"); pause(); continue; }
            g_log.clear();
            if (legacyClaimed(P)) victory(P);   // won save: replay the ending, then new game / quit
            else say("Game loaded. Welcome back, " + P.name + ".");
            playLoop(P);
        } else if (c == '3') manual();
        else return 0;
    }
}
