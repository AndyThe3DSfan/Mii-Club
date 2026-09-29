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
    char name[32]{};
    bool is_cpu = true;
    int score = 0;
    int total_stars = 0;
};

// Global Variables
static PartyMii players[4];
static int total_game_rounds = 3;
static int current_round = 1;
static GameState current_state = STATE_MAIN_MENU;
static int menu_selection = 0;
static MinigameType active_game = GAME_RACE;

// Text-Art Face Generator based on character state
static void printMiiFace(int idx, const char* mood = "normal") {
    if (!players[idx].name[0]) return;
    if (std::strcmp(mood, "win") == 0) {
        printf("  [ ^_^ ]* ");
    } else if (std::strcmp(mood, "lose") == 0) {
        printf("  [ ;_; ]  ");
    } else if (std::strcmp(mood, "action") == 0) {
        printf("  [ >_< ]  ");
    } else {
        printf("  [ o_o ]  ");
    }
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
        std::snprintf(players[slot].name, sizeof(players[slot].name), isCpu ? "CPU %d" : "Player %d", slot + 1);
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

// Universal Button Gate
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
    printf("====================================\n");
    printf("            MII SPRINT              \n");
    printf("====================================\n\n");
    printf("Mash A to run! Tap B to jump barriers!\n");
    printf("Get ready...\n\n");
    svcSleepThread(1500000000ULL);

    int progress[4] = {0, 0, 0, 0};
    int barrier_pos = 15;
    bool won = false;

    while (aptMainLoop() && !won) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        // Human Control
        if (kDown & KEY_A) progress[0] += 1;
        if ((progress[0] == barrier_pos) && !(kDown & KEY_B)) {
            progress[0] -= 2; // Trip barrier penalty
        }

        // Simulating 3 Local CPU Players offline
        for (int i = 1; i < 4; i++) {
            if (std::rand() % 10 < 3) progress[i] += 1; 
        }

        // Render Track Layout
        consoleClear();
        for (int i = 0; i < 4; i++) {
            printf("%s ", players[i].name);
            printMiiFace(i, "action");
            printf("\n|");
            for (int p = 0; p < 30; p++) {
                if (p == progress[i]) printf("M");
                else if (p == barrier_pos) printf("!");
                else printf("_");
            }
            printf("|\n\n");

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
    printf("====================================\n");
    printf("            MII TARGET              \n");
    printf("====================================\n\n");
    printf("Press A when target block reaches [X]!\n");
    svcSleepThread(1500000000ULL);

    int position = 0;
    int direction = 1;
    int target_zone = 8;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        position += direction;
        if (position >= 16 || position <= 0) direction *= -1;

        consoleClear();
        printf("Timing Grid:\n\n[");
        for (int i = 0; i < 17; i++) {
            if (i == target_zone) printf("X");
            else if (i == position) printf("O");
            else printf("-");
        }
        printf("]\n\n");
        printf("Press A now!");

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
    printf("====================================\n");
    printf("           MII BALANCE              \n");
    printf("====================================\n\n");
    printf("Use LEFT/RIGHT D-Pad to stay balanced!\n");
    svcSleepThread(1500000000ULL);

    int balance = 10;
    int survival_ticks = 0;

    while (aptMainLoop() && survival_ticks < 150) {
        hidScanInput();
        u32 kHold = hidKeysHeld();

        if (kHold & KEY_DLEFT) balance -= 1;
        if (kHold & KEY_DRIGHT) balance += 1;

        if (std::rand() % 2 == 0) balance += (std::rand() % 3 - 1);

        consoleClear();
        printf("Stay in Center!\n\nFALL <---[");
        for (int i = 0; i < 20; i++) {
            if (i == balance) printf("M");
            else if (i == 10) printf("|");
            else printf("=");
        }
        printf("]---> FALL\n\n");

        if (balance <= 0 || balance >= 20) {
            printf("You fell off the board!\n");
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
    printf("====================================\n");
    printf("            MII MEMORY              \n");
    printf("====================================\n\n");
    printf("Memorize the keys shown on screen!\n");
    svcSleepThread(1500000000ULL);

    int sequence[3] = { (int)KEY_A, (int)KEY_B, (int)KEY_X };
    
    consoleClear();
    printf("Remember sequence:\n\n     A  ->  B  ->  X\n");
    svcSleepThread(2000000000ULL);
    
    consoleClear();
    printf("Input the keys now!\n");

    int current_input_index = 0;
    while (aptMainLoop() && current_input_index < 3) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & (KEY_A | KEY_B | KEY_X | KEY_Y)) {
            if (kDown & sequence[current_input_index]) {
                current_input_index++;
                printf("Correct!\n");
            } else {
                printf("Wrong button sequence!\n");
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
    printf("====================================\n");
    printf("          ROUND %d RESULTS           \n", current_round);
    printf("====================================\n\n");

    for (int i = 0; i < 4; i++) {
        players[i].total_stars += players[i].score;
        printf("%d. %s ", i + 1, players[i].name);
        printMiiFace(i, players[i].score >= 3 ? "win" : "normal");
        printf(" Earned: +%d Stars (Total: %d)\n", players[i].score, players[i].total_stars);
    }

    printf("\nPress A to proceed...");
    waitForInput(KEY_A);

    current_round++;
    if (current_round > total_game_rounds) {
        current_state = STATE_FINAL_CELEBRATION;
    } else {
        active_game = static_cast<MinigameType>(std::rand() % GAME_COUNT);
        current_state = STATE_PARTY_LOBBY;
    }
}

// ==========================================
//            SCENE ENGINE STATES
// ==========================================

static void showMainMenu() {
    consoleClear();
    printf("====================================\n");
    printf("             MII CLUB 3DS           \n");
    printf("====================================\n\n");
    
    const char* options[5] = { "🎉 Party Mode", "⚡ Quick Game", "👤 Manage Miis", "🏆 Records", "⚙️ Settings" };
    for (int i = 0; i < 5; i++) {
        if (menu_selection == i) printf(" -> [ %s ]\n", options[i]);
        else printf("    %s \n", options[i]);
    }
    printf("\nUse D-Pad Up/Down to Move | A to Select");

    u32 kDown = hidKeysDown();
    if (kDown & KEY_DDOWN) menu_selection = (menu_selection + 1) % 5;
    if (kDown & KEY_DUP) menu_selection = (menu_selection - 1 + 5) % 5;
    if (kDown & KEY_A) {
        if (menu_selection == 0) current_state = STATE_PARTY_SETUP;
        if (menu_selection == 1) {
            initializeDefaultSession();
            active_game = GAME_RACE;
            current_state = STATE_MINIGAME_PLAY;
}
if (menu_selection == 2) {
launchMiiSelector(0, false);
}
}
}
static void showPartySetup() {
consoleClear();
printf("====================================\n");
printf("            PARTY SETUP             \n");
printf("====================================\n\n");
printf(" -> Match Settings:\n");
printf("    Total Rounds: [ %d ]\n\n", total_game_rounds);
printf(" Press LEFT/RIGHT on D-Pad to change rounds\n");
printf(" Press A to Launch Party Lobby!");
u32 kDown = hidKeysDown();
if (kDown & KEY_DLEFT) { if (total_game_rounds > 1) total_game_rounds--; }
if (kDown & KEY_DRIGHT) { if (total_game_rounds < 10) total_game_rounds++; }
if (kDown & KEY_A) {
initializeDefaultSession();
launchMiiSelector(0, false);
for(int i = 1; i < 4; i++) launchMiiSelector(i, true);
current_round = 1;
current_state = STATE_PARTY_LOBBY;
}
}
static void showPartyLobby() {
consoleClear();
printf("====================================\n");
printf("            MII CLUB LOBBY          \n");
printf("====================================\n\n");
printf(" Round %d / %d \n\n", current_round, total_game_rounds);
printf("Current Roster Standings:\n");
for (int i = 0; i < 4; i++) {
printf(" - %s ", players[i].name);
printMiiFace(i, "normal");
printf(" Stars: %d %s\n", players[i].total_stars, players[i].is_cpu ? "[CPU]" : "[YOU]");
}
printf("\nNext Minigame Type Loaded automatically!\n");
printf("Press A to Start Match Run...");
if (hidKeysDown() & KEY_A) {
current_state = STATE_MINIGAME_PLAY;
}
}
static void showFinalCelebration() {
consoleClear();
printf("====================================\n");
printf("          FINAL CELEBRATION!        \n");
printf("====================================\n\n");
printf("The Party Game has concluded!\n\nFinal Scoreboard:\n");
int winner_idx = 0;
int max_stars = -1;
for (int i = 0; i < 4; i++) {
printf(" %s: %d Stars\n", players[i].name, players[i].total_stars);
if (players[i].total_stars > max_stars) {
max_stars = players[i].total_stars;
winner_idx = i;
}
}
printf("\n🏆 WINNER IS: %s! 🏆\n", players[winner_idx].name);
printMiiFace(winner_idx, "win");
printf("\n\nPress A to return to Main Menu...");
if (hidKeysDown() & KEY_A) {
current_state = STATE_MAIN_MENU;
}
}
// ==========================================
//               MAIN SYSTEM ENTRY
// ==========================================
int main() {
gfxInitDefault();
consoleInit(GFX_TOP, NULL);
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
