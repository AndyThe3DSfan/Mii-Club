#include <3ds.h>
#include <3ds/applets/miiselector.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Game States
enum GameState {
    STATE_MAIN_MENU,
    STATE_PARTY_SETUP,
    STATE_PARTY_LOBBY,
    STATE_MINIGAME_PLAY,
    STATE_FINAL_CELEBRATION
};

// Available Minigames
enum MinigameType {
    GAME_RACE,
    GAME_TARGET,
    GAME_BALANCE,
    GAME_MEMORY,
    GAME_COUNT
};

struct PartyMii {
    MiiData data{};
    char name[32];
    bool is_cpu;
    int score;
    int total_stars;
};

// Global Variables
static PartyMii players[4];
static int total_game_rounds = 3;
static int current_round = 1;
static GameState current_state = STATE_MAIN_MENU;
static int menu_selection = 0;
static MinigameType active_game = GAME_RACE;

// --- ANSI COLOR SHORTCUTS ---
#define CLR_RESET   "\x1b[0m"
#define CLR_RED     "\x1b[31;1m"
#define CLR_GREEN   "\x1b[32;1m"
#define CLR_YELLOW  "\x1b[33;1m"
#define CLR_BLUE    "\x1b[34;1m"
#define CLR_MAGENTA "\x1b[35;1m"
#define CLR_CYAN    "\x1b[36;1m"
#define CLR_WHITE   "\x1b[37;1m"

// Nintendo Font Glyphs (3DS Built-In Text Symbols)
#define ICON_MII   "\uE014"
#define ICON_STAR  "\uE001"
#define ICON_CROWN "\uE006"

// Text-Art Avatar Generator with Colors
static void printMiiFace(int idx, const char* mood = "normal") {
    if (players[idx].name[0] == '\0') return;
    
    printf(" %s" CLR_GREEN "[", CLR_RESET);
    if (std::strcmp(mood, "win") == 0) {
        printf(CLR_YELLOW "^_^" CLR_GREEN "]* ");
    } else if (std::strcmp(mood, "lose") == 0) {
        printf(CLR_BLUE ";_;" CLR_GREEN "]  ");
    } else if (std::strcmp(mood, "action") == 0) {
        printf(CLR_RED ">_<" CLR_GREEN "]  ");
    } else {
        printf(CLR_WHITE "o_o" CLR_GREEN "]  ");
    }
    printf("%s", CLR_RESET);
}

// 3DS Applet Mii Selector Access
static bool launchMiiSelector(int slot, bool isCpu) {
    MiiSelectorConf conf;
    MiiSelectorReturn ret;

    miiSelectorInit(&conf);
    miiSelectorSetTitle(&conf, isCpu ? "Select a CPU Mii" : "Select Your Player Mii");
    miiSelectorSetOptions(&conf, MIISELECTOR_CANCEL);

    std::memset(&ret, 0, sizeof(ret));
    miiSelectorLaunch(&conf, &ret);

    if (ret.no_mii_selected || !miiSelectorChecksumIsValid(&ret)) {
        std::snprintf(players[slot].name, sizeof(players[slot].name), isCpu ? "CPU-%d" : "Player-%d", slot + 1);
        players[slot].is_cpu = isCpu;
        return true;
    }

    players[slot].data = ret.mii;
    players[slot].is_cpu = isCpu;
    players[slot].score = 0;
    miiSelectorReturnGetName(&ret, players[slot].name, sizeof(players[slot].name));
    return true;
}

static void initializeDefaultSession() {
    std::srand((unsigned)osGetTime());
    for (int i = 0; i < 4; i++) {
        std::snprintf(players[i].name, sizeof(players[i].name), "Mii-%d", i + 1);
        players[i].is_cpu = (i > 0);
        players[i].score = 0;
        players[i].total_stars = 0;
    }
}

static void waitForInput(u32 key_mask) {
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & key_mask) break;
        gspWaitForVBlank();
    }
}

// ==========================================
//               MINIGAMES CORE
// ==========================================

// Game 1: Mii Race (Obstacle Sprint)
static void playMiiRace() {
    consoleClear();
    printf("\x1b[1;1H" CLR_CYAN "====================================\n");
    printf("            MII SPRINT              \n");
    printf("====================================\n\n" CLR_RESET);
    printf("Mash " CLR_GREEN "A" CLR_RESET " to run! Tap " CLR_YELLOW "B" CLR_RESET " to jump barriers!\n");
    printf("Get ready...\n\n");
    svcSleepThread(1500000000ULL);

    int progress[4] = {0, 0, 0, 0};
    int barrier_pos = 15;
    bool won = false;

    while (aptMainLoop() && !won) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_A) progress[0] += 1;
        if ((progress[0] == barrier_pos) && !(kDown & KEY_B)) {
            progress[0] -= 2; 
        }

        for (int i = 1; i < 4; i++) {
            if (std::rand() % 10 < 3) progress[i] += 1; 
            if (progress[i] == barrier_pos && std::rand() % 5 == 0) progress[i] -= 2; 
        }

        printf("\x1b[5;1H");
        for (int i = 0; i < 4; i++) {
            printf(CLR_GREEN "%-10s " CLR_RESET, players[i].name);
            printMiiFace(i, "action");
            printf("\n" CLR_WHITE "|" CLR_RESET);
            for (int p = 0; p < 30; p++) {
                if (p == progress[i]) printf(CLR_CYAN "M" CLR_RESET);
                else if (p == barrier_pos) printf(CLR_RED "!" CLR_RESET);
                else printf(CLR_WHITE "_" CLR_RESET);
            }
            printf(CLR_WHITE "|\n\n" CLR_RESET);

            if (progress[i] >= 28) {
                players[i].score = 3; 
                for (int j = 0; j < 4; j++) {
                    if (j != i) players[j].score = 1;
                }
                won = true;
            }
        }
        gspWaitForVBlank();
    }
}

// Game 2: Mii Target (Precision Lock)
static void playMiiTarget() {
    consoleClear();
    printf("\x1b[1;1H" CLR_CYAN "====================================\n");
    printf("            MII TARGET              \n");
    printf("====================================\n\n" CLR_RESET);
    printf("Press " CLR_GREEN "A" CLR_RESET " when block reaches [" CLR_RED "X" CLR_RESET "]!\n");
    svcSleepThread(1500000000ULL);

    int position = 0;
    int direction = 1;
    int target_zone = 8;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        position += direction;
        if (position >= 16 || position <= 0) direction *= -1;

        printf("\x1b[6;1H");
        printf("Timing Grid:\n\n" CLR_WHITE "[" CLR_RESET);
        for (int i = 0; i < 17; i++) {
            if (i == target_zone) printf(CLR_RED "X" CLR_RESET);
            else if (i == position) printf(CLR_CYAN "O" CLR_RESET);
            else printf(CLR_WHITE "-" CLR_RESET);
        }
        printf(CLR_WHITE "]\n\n" CLR_RESET);
        printf("Press " CLR_GREEN "A" CLR_RESET " now!  ");

        if (kDown & KEY_A) {
            int accuracy = std::abs(position - target_zone);
            players[0].score = (accuracy == 0) ? 5 : (accuracy <= 2) ? 3 : 1;
            break;
        }
        
        for(int frame=0; frame<4; frame++) gspWaitForVBlank();
    }

    for (int i = 1; i < 4; i++) {
        players[i].score = 1 + (std::rand() % 4);
    }
}

// Game 3: Mii Balance (Platform Centering)
static void playMiiBalance() {
    consoleClear();
    printf("\x1b[1;1H" CLR_CYAN "====================================\n");
    printf("           MII BALANCE              \n");
    printf("====================================\n\n" CLR_RESET);
    printf("Use " CLR_GREEN "LEFT/RIGHT" CLR_RESET " D-Pad to stay balanced!\n");
    svcSleepThread(1500000000ULL);

    int balance = 10;
    int survival_ticks = 0;

    while (aptMainLoop() && survival_ticks < 150) {
        hidScanInput();
        u32 kHold = hidKeysHeld();

        if (kHold & KEY_DLEFT) balance -= 1;
        if (kHold & KEY_DRIGHT) balance += 1;

        if (std::rand() % 2 == 0) balance += (std::rand() % 3 - 1);

        printf("\x1b[6;1H");
        printf("Stay in Center!\n\n" CLR_RED "FALL" CLR_WHITE " <---[" CLR_RESET);
        for (int i = 0; i < 20; i++) {
            if (i == balance) printf(CLR_CYAN "M" CLR_RESET);
            else if (i == 10) printf(CLR_YELLOW "|" CLR_RESET);
            else printf(CLR_WHITE "=" CLR_RESET);
        }
        printf(CLR_WHITE "]" CLR_RED "---> FALL\n\n" CLR_RESET);

        if (balance <= 0 || balance >= 20) {
            printf(CLR_RED "You fell off the board!        \n" CLR_RESET);
            break;
        }

        survival_ticks++;
        gspWaitForVBlank();
    }

    players[0].score = (survival_ticks / 30);
    for (int i = 1; i < 4; i++) players[i].score = 1 + (std::rand() % 4);
}

// Game 4: Mii Memory (Button Sequence Match)
static void playMiiMemory() {
    consoleClear();
    printf("\x1b[1;1H" CLR_CYAN "====================================\n");
    printf("            MII MEMORY              \n");
    printf("====================================\n\n" CLR_RESET);
    printf("Memorize the keys shown on screen!\n");
    svcSleepThread(1500000000ULL);

    int sequence[3] = { (int)KEY_A, (int)KEY_B, (int)KEY_X };
    
    consoleClear();
    printf("\x1b[1;1H" CLR_YELLOW "Remember sequence:\n\n" CLR_GREEN "     A  " CLR_WHITE "->" CLR_RED "  B  " CLR_WHITE "->" CLR_BLUE "  X\n" CLR_RESET);
    svcSleepThread(2000000000ULL);
    
    consoleClear();
    printf("\x1b[1;1HInput the keys now!\n\n");

    int current_input_index = 0;
    while (aptMainLoop() && current_input_index < 3) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & (KEY_A | KEY_B | KEY_X | KEY_Y)) {
            if (kDown & sequence[current_input_index]) {
                current_input_index++;
                printf(CLR_GREEN "Correct!   \n" CLR_RESET);
            } else {
                printf(CLR_RED "Wrong button sequence!   \n" CLR_RESET);
                svcSleepThread(1000000000ULL);
                break;
            }
        }
        gspWaitForVBlank();
    }

    players[0].score = current_input_index * 2;
    for (int i = 1; i < 4; i++) players[i].score = (std::rand() % 3) * 2;
}

static void processMinigameResults() {
    consoleClear();
    printf("\x1b[1;1H" CLR_CYAN "====================================\n");
    printf("          ROUND %d RESULTS           \n", current_round);
    printf("====================================\n\n" CLR_RESET);

    for (int i = 0; i < 4; i++) {
        players[i].total_stars += players[i].score;
        printf("%d. " CLR_GREEN "%-10s " CLR_RESET, i + 1, players[i].name);
        printMiiFace(i, players[i].score >= 3 ? "win" : "normal");
printf(" Earned: " CLR_YELLOW "+%d %s " CLR_RESET "(Total: " CLR_YELLOW "%d" CLR_RESET ")\n", players[i].score, ICON_STAR, players[i].total_stars);
}
printf("\nPress " CLR_GREEN "A" CLR_RESET " to proceed...");
gfxFlushBuffers();
gspWaitForVBlank();
waitForInput(KEY_A);
current_round++;
if (current_round > total_game_rounds) {
current_state = STATE_FINAL_CELEBRATION;
} else {
active_game = static_cast(std::rand() % GAME_COUNT);
current_state = STATE_PARTY_LOBBY;
}
}
// ==========================================
//            SCENE ENGINE STATES
// ==========================================
static void showMainMenu() {
printf("\x1b[1;1H");
printf(CLR_CYAN "====================================\n");
printf("             MII CLUB 3DS           \n");
printf("====================================\n\n" CLR_RESET);
const char* options[5] = { "🎉 Party Mode ", "⚡ Quick Game ", "👤 Manage Miis", "🏆 Records    ", "⚙️ Settings   " };
for (int i = 0; i < 5; i++) {
if (menu_selection == i) printf(CLR_YELLOW " -> [ %s ]\n" CLR_RESET, options[i]);
else printf("    %s   \n", options[i]);
}
printf("\nUse " CLR_GREEN "D-Pad Up/Down" CLR_RESET " to Move | " CLR_GREEN "A" CLR_RESET " to Select\n");
printf("                                           \n");
u32 kDown = hidKeysDown();
if (kDown & KEY_DDOWN) menu_selection = (menu_selection + 1) % 5;
if (kDown & KEY_DUP) menu_selection = (menu_selection - 1 + 5) % 5;
if (kDown & KEY_A) {
consoleClear();
if (menu_selection == 0) current_state = STATE_PARTY_SETUP;
if (menu_selection == 1) {
initializeDefaultSession();
active_game = GAME_RACE;
current_state = STATE_MINIGAME_PLAY;
}
if (menu_selection == 2) {
initializeDefaultSession();
launchMiiSelector(0, false);
}
}
}
static void showPartySetup() {
printf("\x1b[1;1H");
printf(CLR_CYAN "====================================\n");
printf("            PARTY SETUP             \n");
printf("====================================\n\n" CLR_RESET);
printf(" -> Match Settings:\n");
printf("    Total Rounds: [ " CLR_YELLOW "%d" CLR_RESET " ] \n\n", total_game_rounds);
printf(" Press " CLR_GREEN "LEFT/RIGHT" CLR_RESET " on D-Pad to change rounds\n");
printf(" Press " CLR_GREEN "A" CLR_RESET " to Launch Profile Configuration!\n");
printf("                                           \n");
u32 kDown = hidKeysDown();
if (kDown & KEY_DLEFT) { if (total_game_rounds > 1) total_game_rounds--; }
if (kDown & KEY_DRIGHT) { if (total_game_rounds < 10) total_game_rounds++; }
if (kDown & KEY_A) {
consoleClear();
initializeDefaultSession();
launchMiiSelector(0, false);
for(int i = 1; i < 4; i++) launchMiiSelector(i, true);
current_round = 1;
current_state = STATE_PARTY_LOBBY;
consoleClear();
}
}
static void showPartyLobby() {
printf("\x1b[1;1H");
printf(CLR_CYAN "====================================\n");
printf("            MII CLUB LOBBY          \n");
printf("====================================\n\n" CLR_RESET);
printf(" Round " CLR_YELLOW "%d" CLR_RESET " / " CLR_YELLOW "%d" CLR_RESET " \n\n", current_round, total_game_rounds);
printf("Current Roster Standings:\n");
for (int i = 0; i < 4; i++) {
printf(" - " CLR_GREEN "%-10s " CLR_RESET, players[i].name);
printMiiFace(i, "normal");
printf(" " CLR_YELLOW "%s" CLR_RESET ": " CLR_YELLOW "%d" CLR_RESET " %s\n", ICON_STAR, players[i].total_stars, players[i].is_cpu ? CLR_BLUE "[CPU]" : CLR_GREEN "[YOU]");
}
printf("\nNext Minigame Loaded automatically!\n");
printf("Press " CLR_GREEN "A" CLR_RESET " to Start Match Run...       \n");
if (hidKeysDown() & KEY_A) {
consoleClear();
current_state = STATE_MINIGAME_PLAY;
}
}
static void showFinalCelebration() {
printf("\x1b[1;1H");
printf(CLR_CYAN "====================================\n");
printf("          FINAL CELEBRATION!        \n");
printf("====================================\n\n" CLR_RESET);
printf("The Party Game has concluded!\n\nFinal Scoreboard:\n");
int winner_idx = 0;
int max_stars = -1;
for (int i = 0; i < 4; i++) {
printf(" " CLR_GREEN "%-10s" CLR_RESET ": " CLR_YELLOW "%d %s\n" CLR_RESET, players[i].name, players[i].total_stars, ICON_STAR);
if (players[i].total_stars > max_stars) {
max_stars = players[i].total_stars;
winner_idx = i;
}
}
printf("\n" CLR_YELLOW "%s WINNER IS: %s! %s\n" CLR_RESET, ICON_CROWN, players[winner_idx].name, ICON_CROWN);
printMiiFace(winner_idx, "win");
printf("\n\nPress " CLR_GREEN "A" CLR_RESET " to return to Main Menu...           \n");
if (hidKeysDown() & KEY_A) {
consoleClear();
current_state = STATE_MAIN_MENU;
}
}
// ==========================================
//               MAIN SYSTEM ENTRY
// ==========================================
int main() {
gfxInitDefault();
consoleInit(GFX_TOP, NULL);
consoleClear();
current_state = STATE_MAIN_MENU;
while (aptMainLoop()) {
hidScanInput();
u32 kDown = hidKeysDown();
if (kDown & KEY_START) break;
switch (current_state) {
case STATE_MAIN_MENU:
showMainMenu();
break;
case STATE_PARTY_SETUP:
showPartySetup();
break;
case STATE_PARTY_LOBBY:
showPartyLobby();
break;
case STATE_MINIGAME_PLAY:
if (active_game == GAME_RACE) playMiiRace();
else if (active_game == GAME_TARGET) playMiiTarget();
else if (active_game == GAME_BALANCE) playMiiBalance();
else if (active_game == GAME_MEMORY) playMiiMemory();
processMinigameResults();
break;
case STATE_FINAL_CELEBRATION:
showFinalCelebration();
break;
}
gfxFlushBuffers();
gfxSwapBuffers();
gspWaitForVBlank();
}
gfxExit();
return 0;
}
