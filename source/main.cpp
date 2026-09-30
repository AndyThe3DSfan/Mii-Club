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
    GAME_DRAWING,
    GAME_COUNT
};

struct PartyMii {
    MiiData data;
    char name[32]; 
    bool is_cpu;
    int score;
    int total_stars;
};

// FIXED: Defined four separate variables so iPad copy-paste cannot delete array brackets!
static PartyMii mii1;
static PartyMii mii2;
static PartyMii mii3;
static PartyMii mii4;

static int total_game_rounds = 3;
static int current_round = 1;
static GameState current_state = STATE_MAIN_MENU;
static int menu_selection = 0;
static MinigameType active_game = GAME_RACE;

// --- AUDIO ENGINE GLOBAL VARIABLES ---
static FILE* wave_file = NULL;
static u8* audio_buffer = NULL;
static u32 audio_size = 0;
static ndspWaveBuf wave_block;

// --- ANSI COLOR SHORTCUTS ---
#define CLR_RESET   "\x1b[0m"
#define CLR_RED     "\x1b[31;1m"
#define CLR_GREEN   "\x1b[32;1m"
#define CLR_YELLOW  "\x1b[33;1m"
#define CLR_BLUE    "\x1b[34;1m"
#define CLR_MAGENTA "\x1b[35;1m"
#define CLR_CYAN    "\x1b[36;1m"
#define CLR_WHITE   "\x1b[37;1m"

// Text Glyphs
#define ICON_MII   "[Mii]"
#define ICON_STAR  "*"
#define ICON_CROWN "[WIN]"

// --- AUDIO PLAYER CONTROLLER ---
static void initBackgroundMusic() {
    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);

    wave_file = fopen("bgm.wav", "rb");
    if (wave_file == NULL) {
        wave_file = fopen("sdmc:/3ds/bgm.wav", "rb");
    }

    if (wave_file != NULL) {
        fseek(wave_file, 44, SEEK_SET);

        audio_buffer = (u8*)linearAlloc(2 * 1024 * 1024);
        if (audio_buffer != NULL) {
            audio_size = fread(audio_buffer, 1, 2 * 1024 * 1024, wave_file);

            DSP_FlushDataCache(audio_buffer, audio_size);

            std::memset(&wave_block, 0, sizeof(ndspWaveBuf));
            wave_block.data_vaddr = audio_buffer;
            wave_block.nsamples = audio_size / 4; 
            wave_block.looping = true;            

            ndspChnWaveBufAdd(0, &wave_block);
        }
        fclose(wave_file);
    }
}

static void exitAudioEngine() {
    ndspChnReset(0);
    if (audio_buffer != NULL) {
        linearFree(audio_buffer);
    }
    ndspExit();
}

static void printMiiFace(int idx, const char* mood = "normal") {
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

// Helper to launch applet on distinct profiles
static bool launchMiiSelector(int slot, bool isCpu) {
    MiiSelectorConf conf;
    MiiSelectorReturn ret;

    miiSelectorInit(&conf);
    miiSelectorSetTitle(&conf, isCpu ? "Select a CPU Mii" : "Select Your Player Mii");
    miiSelectorSetOptions(&conf, MIISELECTOR_CANCEL);

    std::memset(&ret, 0, sizeof(ret));
    miiSelectorLaunch(&conf, &ret);

    PartyMii* target = (slot == 0) ? &mii1 : (slot == 1) ? &mii2 : (slot == 2) ? &mii3 : &mii4;

    if (ret.no_mii_selected || !miiSelectorChecksumIsValid(&ret)) {
        std::snprintf(target->name, sizeof(target->name), isCpu ? "CPU-%d" : "Player-%d", slot + 1);
        target->is_cpu = isCpu;
        return true;
    }

    target->data = ret.mii;
    target->is_cpu = isCpu;
    target->score = 0;
    miiSelectorReturnGetName(&ret, target->name, sizeof(target->name));
    return true;
}

static void initializeDefaultSession() {
    std::srand((unsigned)osGetTime());
    
    std::snprintf(mii1.name, sizeof(mii1.name), "Player-1");
    mii1.is_cpu = false; mii1.score = 0; mii1.total_stars = 0;

    std::snprintf(mii2.name, sizeof(mii2.name), "CPU-2");
    mii2.is_cpu = true; mii2.score = 0; mii2.total_stars = 0;

    std::snprintf(mii3.name, sizeof(mii3.name), "CPU-3");
    mii3.is_cpu = true; mii3.score = 0; mii3.total_stars = 0;

    std::snprintf(mii4.name, sizeof(mii4.name), "CPU-4");
    mii4.is_cpu = true; mii4.score = 0; mii4.total_stars = 0;
}

static void waitForInputWithDelay(u32 key_mask) {
    gfxFlushBuffers();
    gspWaitForVBlank();
    svcSleepThread(1000000000ULL); 
    
    hidScanInput();
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & key_mask) break;
        gspWaitForVBlank();
    }
}

static void showFinishScreen() {
    consoleClear();
    printf("\x1b[5;1H" CLR_RED "====================================\n");
    printf("              FINISH!               \n");
    printf("====================================\n\n" CLR_RESET);
    printf("          Time is up!\n\n");
    printf("Press " CLR_GREEN "A" CLR_RESET " to calculate standings...");
    waitForInputWithDelay(KEY_A);
}

// ==========================================
//               MINIGAMES CORE
// ==========================================

static void playMiiRace() {
    consoleClear();
    printf("\x1b[1;1H" CLR_CYAN "====================================\n");
    printf("            MII SPRINT              \n");
    printf("====================================\n\n" CLR_RESET);
    printf("Mash " CLR_GREEN "A" CLR_RESET " to run! Tap " CL_YELLOW "B" CLR_RESET " to jump barriers!\n");
    printf("Get ready...\n\n");
    svcSleepThread(1500000000ULL);

    int p1 = 0, p2 = 0, p3 = 0, p4 = 0;
    int barrier_pos = 15;
    bool won = false;
    int loop_counter = 0;

    while (aptMainLoop() && !won) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_A) p1 += 1;
        if ((p1 == barrier_pos) && !(kDown & KEY_B)) p1 -= 2; 

        if (loop_counter % 8 == 0) {
            if (std::rand() % 10 < 4) p2 += 1;
            if (p2 == barrier_pos && std::rand() % 6 == 0) p2 -= 2;

            if (std::rand() % 10 < 4) p3 += 1;
            if (p3 == barrier_pos && std::rand() % 6 == 0) p3 -= 2;

            if (std::rand() % 10 < 4) p4 += 1;
            if (p4 == barrier_pos && std::rand() % 6 == 0) p4 -= 2;
        }

        printf("\x1b[6;1H");
        
        // Render Player 1
        printf(CLR_GREEN "%-10s " CLR_RESET, mii1.name); printMiiFace(0, "action");
        printf("\n|");
        for (int p = 0; p < 30; p++) { if (p == p1) printf("M"); else if (p == barrier_pos) printf("!"); else printf("_"); }
        printf("|\n\n");

        // Render Player 2
        printf(CLR_GREEN "%-10s " CLR_RESET, mii2.name); printMiiFace(1, "action");
        printf("\n|");
        for (int p = 0; p < 30; p++) { if (p == p2) printf("M"); else if (p == barrier_pos) printf("!"); else printf("_"); }
        printf("|\n\n");

        // Render Player 3
        printf(CLR_GREEN "%-10s " CLR_RESET, mii3.name); printMiiFace(2, "action");
        printf("\n|");
        for (int p = 0; p < 30; p++) { if (p == p3) printf("M"); else if (p == barrier_pos) printf("!"); else printf("_"); }
        printf("|\n\n");

        // Render Player 4
        printf(CLR_GREEN "%-10s " CLR_RESET, mii4.name); printMiiFace(3, "action");
        printf("\n|");
        for (int p = 0; p < 30; p++) { if (p == p4) printf("M"); else if (p == barrier_pos) printf("!"); else printf("_"); }
        printf("|\n\n");

        if (p1 >= 28 || p2 >= 28 || p3 >= 28 || p4 >= 28) {
            if (p1 >= 28) { mii1.score = 3; mii2.score = 1; mii3.score = 1; mii4.score = 1; }
            else if (p2 >= 28) { mii1.score = 1; mii2.score = 3; mii3.score = 1; mii4.score = 1; }
            else if (p3 >= 28) { mii1.score = 1; mii2.score = 1; mii3.score = 3; mii4.score = 1; }
            else { mii1.score = 1; mii2.score = 1; mii3.score = 1; mii4.score = 3; }
            won = true;
        }
        loop_counter++;
        gspWaitForVBlank();
    }
    showFinishScreen();
}

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
    int duration_ticks = 0;
    bool player_hit = false;

    while (aptMainLoop() && duration_ticks < 300) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (duration_ticks % 6 == 0) {
            position += direction;
            if (position >= 16 || position <= 0) direction *= -1;
        }

        printf("\x1b[6;1H");
        printf("Timing Grid:\n\n" CLR_WHITE "[" CLR_RESET);
        for (int i = 0; i < 17; i++) {
            if (i == target_zone) printf(CLR_RED "X" CLR_RESET);
            else if (i == position) printf(CLR_CYAN "O" CLR_RESET);
            else printf(CLR_WHITE "-" CLR_RESET);
        }
        printf(CLR_WHITE "]\n\n" CLR_RESET);
        printf("Press " CLR_GREEN "A" CLR_RESET " now!  \n\n");

        if ((kDown & KEY_A) && !player_hit) {
            int accuracy = std::abs(position - target_zone);
            mii1.score = (accuracy == 0) ? 5 : (accuracy <= 2) ? 3 : 1;
            player_hit = true;
        }
        
        duration_ticks++;
        gspWaitForVBlank();
    }

    if (!player_hit) mii1.score = 0;
    mii2.score = 1 + (std::rand() % 4);
    mii3.score = 1 + (std::rand() % 4);
    mii4.score = 1 + (std::rand() % 4);
    showFinishScreen();
}

static void playMiiBalance() {
    consoleClear();
printf("\x1b[1;1H" CLR_CYAN "====================================\n");
printf("           MII BALANCE              \n");
printf("====================================\n\n" CLR_RESET);
printf("Use " CLR_GREEN "LEFT/RIGHT" CLR_RESET " D-Pad to stay balanced!\n");
svcSleepThread(1500000000ULL);
int balance = 10;
int survival_ticks = 0;
while (aptMainLoop() && survival_ticks < 300) {
hidScanInput();
u32 kHold = hidKeysHeld();
if (survival_ticks % 3 == 0) {
if (kHold & KEY_DLEFT) balance -= 1;
if (kHold & KEY_DRIGHT) balance += 1;
if (std::rand() % 2 == 0) balance += (std::rand() % 3 - 1);
}
printf("\x1b[6;1H");
printf("Stay in Center!\n\n" CLR_RED "FALL" CLR_WHITE " <---[" CLR_RESET);
for (int i = 0; i < 20; i++) {
if (i == balance) printf(CLR_CYAN "M" CLR_RESET);
else if (i == 10) printf(CLR_YELLOW "|" CLR_RESET);
else printf(CLR_WHITE "=" CLR_RESET);
}
printf(CLR_WHITE "]" CLR_RED "---> FALL\n\n" CLR_RESET);
if (balance <= 0 || balance >= 20) {
break;
}
survival_ticks++;
gspWaitForVBlank();
}
mii1.score = (survival_ticks / 60);
mii2.score = 1 + (std::rand() % 4);
mii3.score = 1 + (std::rand() % 4);
mii4.score = 1 + (std::rand() % 4);
showFinishScreen();
}
static void playMiiMemory() {
consoleClear();
printf("\x1b[1;1H" CLR_CYAN "====================================\n");
printf("            MII MEMORY              \n");
printf("====================================\n\n" CLR_RESET);
printf("Memorize the keys shown on screen!\n");
svcSleepThread(1500000000ULL);
int choice = std::rand() % 3;
const char* pattern = (choice == 0) ? "A - B - X" : (choice == 1) ? "X - Y - A" : "B - B - Y";
consoleClear();
printf("\x1b[1;1H" CLR_YELLOW "Remember sequence:\n\n" CLR_GREEN "     %s\n\n" CLR_RESET, pattern);
svcSleepThread(3000000000ULL);
consoleClear();
printf("\x1b[1;1HInput the keys now!\n\n");
int current_input_index = 0;
int input_timeout = 0;
while (aptMainLoop() && current_input_index < 3 && input_timeout < 300) {
hidScanInput();
u32 kDown = hidKeysDown();
if (kDown & (KEY_A | KEY_B | KEY_X | KEY_Y)) {
current_input_index++;
printf(CLR_GREEN "Key Pressed!\n" CLR_RESET);
}
input_timeout++;
gspWaitForVBlank();
}
mii1.score = current_input_index * 2;
mii2.score = (std::rand() % 3) * 2;
mii3.score = (std::rand() % 3) * 2;
mii4.score = (std::rand() % 3) * 2;
showFinishScreen();
}
static void playMiiDrawing() {
consoleClear();
printf("\x1b[1;1H" CLR_CYAN "====================================\n");
printf("            MII DRAWING             \n");
printf("====================================\n\n" CLR_RESET);
printf("Hold " CLR_GREEN "A" CLR_RESET " to sketch your masterpiece design!\n");
svcSleepThread(1500000000ULL);
int drawing_ticks = 0;
while (aptMainLoop() && drawing_ticks < 300) {
hidScanInput();
u32 kHold = hidKeysHeld();
printf("\x1b[6;1H");
printf("Drawing Canvas Profile Layout:\n\n");
if (kHold & KEY_A) {
printf(CLR_YELLOW "   ( o_o)---7  SKETCHING \n" CLR_RESET);
printf("   /|   |\\                 \n");
printf("    ||                  \n");
} else {
printf(CLR_WHITE "   ( --)      IDLE       \n" CLR_RESET);
printf("   /|   |\\                 \n");
printf("    |_|                  \n");
}
drawing_ticks++;
gspWaitForVBlank();
}
consoleClear();
printf("\x1b[4;1H" CLR_GREEN "Your Mii presents the finished drawing art! \n\n" CLR_RESET);
printf("Press A to submit to judges...");
waitForInputWithDelay(KEY_A);
mii1.score = 3 + (std::rand() % 3);
mii2.score = 2 + (std::rand() % 4);
mii3.score = 2 + (std::rand() % 4);
mii4.score = 2 + (std::rand() % 4);
showFinishScreen();
}
static void processMinigameResults() {
consoleClear();
printf("\x1b[1;1H" CLR_CYAN "====================================\n");
printf("          ROUND %d RESULTS           \n", current_round);
printf("====================================\n\n" CLR_RESET);
mii1.total_stars += mii1.score;
mii2.total_stars += mii2.score;
mii3.total_stars += mii3.score;
mii4.total_stars += mii4.score;
printf("1. " CLR_GREEN "%-10s " CLR_RESET, mii1.name); printMiiFace(0); printf(" Earned: +%d (Total: %d)\n", mii1.score, mii1.total_stars);
printf("2. " CLR_GREEN "%-10s " CLR_RESET, mii2.name); printMiiFace(1); printf(" Earned: +%d (Total: %d)\n", mii2.score, mii2.total_stars);
printf("3. " CLR_GREEN "%-10s " CL_RESET, mii3.name); printMiiFace(2); printf(" Earned: +%d (Total: %d)\n", mii3.score, mii3.total_stars);
printf("4. " CLR_GREEN "%-10s " CL_RESET, mii4.name); printMiiFace(3); printf(" Earned: +%d (Total: %d)\n", mii4.score, mii4.total_stars);
printf("\nPress %sA%s to proceed...", CLR_GREEN, CLR_RESET);
waitForInputWithDelay(KEY_A);
current_round++;
if (current_round > total_game_rounds) {
current_state = STATE_FINAL_CELEBRATION;
} else {
int next_game = std::rand() % (int)GAME_COUNT;
active_game = (MinigameType)next_game;
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
const char* options[5] = { "Party Mode   ", "Quick Game   ", "Manage Miis  ", "Records      ", "Settings     " };
for (int i = 0; i < 5; i++) {
if (menu_selection == i) printf(CLR_YELLOW " -> [ %s ]\n" CLR_RESET, options[i]);
else printf("    %s   \n", options[i]);
}
printf("\nUse %sD-Pad Up/Down%s to Move | %sA%s to Select\n", CLR_GREEN, CLR_RESET, CLR_GREEN, CLR_RESET);
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
printf("    Total Rounds: [ %s%d%s ] \n\n", CLR_YELLOW, total_game_rounds, CLR_RESET);
printf(" Press %sLEFT/RIGHT%s on D-Pad to change rounds\n", CLR_GREEN, CLR_RESET);
printf(" Press %sA%s to Launch Profile Configuration!\n", CLR_GREEN, CLR_RESET);
u32 kDown = hidKeysDown();
if (kDown & KEY_DLEFT) { if (total_game_rounds > 1) total_game_rounds--; }
if (kDown & KEY_DRIGHT) { if (total_game_rounds < 10) total_game_rounds++; }
if (kDown & KEY_A) {
consoleClear();
initializeDefaultSession();
launchMiiSelector(0, false);
launchMiiSelector(1, true);
launchMiiSelector(2, true);
launchMiiSelector(3, true);
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
printf(" Round %s%d%s / %s%d%s \n\n", CLR_YELLOW, current_round, CLR_RESET, CLR_YELLOW, total_game_rounds, CLR_RESET);
printf("Current Roster Standings:\n");
printf(" - %s%-10s %s Stars: %d\n", CLR_GREEN, mii1.name, CLR_RESET, mii1.total_stars);
printf(" - %s%-10s %s Stars: %d\n", CLR_GREEN, mii2.name, CLR_RESET, mii2.total_stars);
printf(" - %s%-10s %s Stars: %d\n", CLR_GREEN, mii3.name, CLR_RESET, mii3.total_stars);
printf(" - %s%-10s %s Stars: %d\n", CLR_GREEN, mii4.name, CLR_RESET, mii4.total_stars);
printf("\nNext Minigame Loaded automatically!\n");
printf("Press %sA%s to Start Match Run...\n", CLR_GREEN, CLR_RESET);
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
printf(" %s%-10s%s: %s%d\n", CLR_GREEN, mii1.name, CLR_RESET, CLR_YELLOW, mii1.total_stars);
printf(" %s%-10s%s: %s%d\n", CLR_GREEN, mii2.name, CLR_RESET, CLR_YELLOW, mii2.total_stars);
printf(" %s%-10s%s: %s%d\n", CLR_GREEN, mii3.name, CLR_RESET, CLR_YELLOW, mii3.total_stars);
printf(" %s%-10s%s: %s%d\n", CLR_GREEN, mii4.name, CLR_RESET, CLR_YELLOW, mii4.total_stars);
int win_stars = mii1.total_stars;
const char* win_name = mii1.name;
if (mii2.total_stars > win_stars) { win_stars = mii2.total_stars; win_name = mii2.name; }
if (mii3.total_stars > win_stars) { win_stars = mii3.total_stars; win_name = mii3.name; }
if (mii4.total_stars > win_stars) { win_stars = mii4.total_stars; win_name = mii4.name; }
printf("\n%s WINNER IS: %s! %s\n", CLR_YELLOW ICON_CROWN, win_name, ICON_CROWN CLR_RESET);
printf("\n\nPress %sA%s to return to Main Menu...\n", CLR_GREEN, CLR_RESET);
if (hidKeysDown() & KEY_A) {
consoleClear();
current_state = STATE_MAIN_MENU;
}
}
int main() {
gfxInitDefault();
consoleInit(GFX_TOP, NULL);
initBackgroundMusic();
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
else if (active_game == GAME_DRAWING) playMiiDrawing();
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
exitAudioEngine();
gfxExit();
return 0;
}
