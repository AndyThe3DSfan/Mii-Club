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

// Available Minigames (All 5 fully enabled!)
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
    char name; 
    bool is_cpu;
    int score;
    int total_stars;
};

// Global Variables
static PartyMii players; 
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
    // FIXED: Changed to the official libctru name 'ndspChnSetInterp' to resolve line 19 build error!
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

// Text-Art Avatar Generator with Colors
static void printMiiFace(int idx, const char* mood = "normal") {
    if (players[idx].name == '\0') return;
    
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

// Universal Button Gate with forced 1-Second anti-skip protective timer
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

// Game 1: Mii Race (Obstacle Sprint)
static void playMiiRace() {
    consoleClear();
    printf("\x1b[1;1H" CLR_CYAN "====================================\n");
    printf("            MII SPRINT              \n");
    printf("====================================\n\n" CLR_RESET);
    printf("Mash " CLR_GREEN "A" CLR_RESET " to run! Tap " CLR_YELLOW "B" CLR_RESET " to jump barriers!\n");
    printf("Get ready...\n\n");
    svcSleepThread(1500000000ULL);

    int progress = {0, 0, 0, 0}; 
    int barrier_pos = 15;
    bool won = false;
    int loop_counter = 0;

    while (aptMainLoop() && !won) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_A) progress += 1;
        if ((progress == barrier_pos) && !(kDown & KEY_B)) progress -= 2; 

        if (loop_counter % 8 == 0) {
            for (int i = 1; i < 4; i++) {
                if (std::rand() % 10 < 4) progress[i] += 1; 
                if (progress[i] == barrier_pos && std::rand() % 6 == 0) progress[i] -= 2; 
            }
        }

        printf("\x1b[6;1H");
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
                for (int j = 0; j < 4; j++) { if (j != i) players[j].score = 1; }
                won = true;
            }
        }
        loop_counter++;
        gspWaitForVBlank();
    }
    showFinishScreen();
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
        printf("Time Remaining: %d   ", (300 - duration_ticks) / 60);

        if ((kDown & KEY_A) && !player_hit) {
            int accuracy = std::abs(position - target_zone);
            players.score = (accuracy == 0) ? 5 : (accuracy <= 2) ? 3 : 1;
            player_hit = true;
        }
        
        duration_ticks++;
        gspWaitForVBlank();
    }

    if (!player_hit) players.score = 0;

    for (int i = 1; i < 4; i++) players[i].score = 1 + (std::rand() % 4);
    showFinishScreen();
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
printf("Time Survived: %d Seconds ", survival_ticks / 60);
if (balance <= 0 || balance >= 20) {
printf(CLR_RED "\nYou fell off the board!        \n" CLR_RESET);
svcSleepThread(1000000000ULL);
break;
}
survival_ticks++;
gspWaitForVBlank();
}
players.score = (survival_ticks / 60);
for (int i = 1; i < 4; i++) players[i].score = 1 + (std::rand() % 4);
showFinishScreen();
}
// Game 4: Mii Memory (Button Sequence Match)
static void playMiiMemory() {
consoleClear();
printf("\x1b[1;1H" CLR_CYAN "====================================\n");
printf("            MII MEMORY              \n");
printf("====================================\n\n" CLR_RESET);
printf("Memorize the keys shown on screen!\n");
svcSleepThread(1500000000ULL);
int variations = {
{ (int)KEY_A, (int)KEY_B, (int)KEY_X },
{ (int)KEY_X, (int)KEY_Y, (int)KEY_A },
{ (int)KEY_DLEFT, (int)KEY_DRIGHT, (int)KEY_A },
{ (int)KEY_B, (int)KEY_B, (int)KEY_Y },
{ (int)KEY_X, (int)KEY_B, (int)KEY_X }
};
const char* text_variations = {
"A  ->  B  ->  X",
"X  ->  Y  ->  A",
"LEFT -> RIGHT -> A",
"B  ->  B  ->  Y",
"X  ->  B  ->  X"
};
int choice = std::rand() % 5;
consoleClear();
printf("\x1b[1;1H" CLR_YELLOW "Remember sequence:\n\n" CLR_GREEN "     %s\n\n" CLR_RESET, text_variations[choice]);
printf("Displaying sequence for 3 seconds...");
svcSleepThread(3000000000ULL);
consoleClear();
printf("\x1b[1;1HInput the keys now!\n\n");
int current_input_index = 0;
int input_timeout = 0;
while (aptMainLoop() && current_input_index < 3 && input_timeout < 300) {
hidScanInput();
u32 kDown = hidKeysDown();
if (kDown & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_DLEFT | KEY_DRIGHT)) {
if (kDown & variations[choice][current_input_index]) {
current_input_index++;
printf(CLR_GREEN "Correct!   \n" CLR_RESET);
} else {
printf(CLR_RED "Wrong button sequence!   \n" CLR_RESET);
svcSleepThread(1000000000ULL);
break;
}
}
input_timeout++;
gspWaitForVBlank();
}
players.score = current_input_index * 2;
for (int i = 1; i < 4; i++) players[i].score = (std::rand() % 3) * 2;
showFinishScreen();
}
// Game 5: Mii Drawing (Showcase Presenter)
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
printf("\nSketch Progress: [");
for(int i=0; i<30; i++) {
if (i < (drawing_ticks/10)) printf("#");
else printf("-");
}
printf("] %d%%", drawing_ticks / 3);
drawing_ticks++;
gspWaitForVBlank();
}
consoleClear();
printf("\x1b[4;1H" CLR_GREEN "Your Mii presents the finished drawing art! \n\n" CLR_RESET);
printf("      __________________ \n");
printf("     |  " CLR_CYAN "MII MASTERPIECE" CLR_RESET " |\n");
printf("     |    \\( ^^ )/     |\n");
printf("     |_________________| \n\n");
printf("Press A to submit to judges...");
waitForInputWithDelay(KEY_A);
players.score = 3 + (std::rand() % 3);
for (int i = 1; i < 4; i++) players[i].score = 2 + (std::rand() % 4);
showFinishScreen();
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
printf("\nPress %sA%s to proceed...", CLR_GREEN, CLR_RESET);
waitForInputWithDelay(KEY_A);
current_round++;
if (current_round > total_game_rounds) {
current_state = STATE_FINAL_CELEBRATION;
} else {
int next_game = std::rand() % (int)GAME_COUNT;
// MOBILE SAFE TYPE CAST INJECTED FLUSH
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
const char* options = { "Party Mode   ", "Quick Game   ", "Manage Miis  ", "Records      ", "Settings     " };
for (int i = 0; i < 5; i++) {
if (menu_selection == i) printf(CLR_YELLOW " -> [ %s ]\n" CLR_RESET, options[i]);
else printf("    %s   \n", options[i]);
}
printf("\nUse %sD-Pad Up/Down%s to Move | %sA%s to Select\n", CLR_GREEN, CLR_RESET, CLR_GREEN, CLR_RESET);
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
printf("    Total Rounds: [ %s%d%s ] \n\n", CLR_YELLOW, total_game_rounds, CLR_RESET);
printf(" Press %sLEFT/RIGHT%s on D-Pad to change rounds\n", CLR_GREEN, CLR_RESET);
printf(" Press %sA%s to Launch Profile Configuration!\n", CLR_GREEN, CLR_RESET);
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
printf(" Round %s%d%s / %s%d%s \n\n", CLR_YELLOW, current_round, CLR_RESET, CLR_YELLOW, total_game_rounds, CLR_RESET);
printf("Current Roster Standings:\n");
for (int i = 0; i < 4; i++) {
printf(" - %s%-10s %s", CLR_GREEN, players[i].name, CLR_RESET);
printMiiFace(i, "normal");
printf(" %s%s%s: %s%d%s %s\n", CLR_YELLOW, ICON_STAR, CLR_RESET, CLR_YELLOW, players[i].total_stars, CLR_RESET, players[i].is_cpu ? CLR_BLUE "[CPU]" : CLR_GREEN "[YOU]");
}
printf("\nNext Minigame Loaded automatically!\n");
printf("Press %sA%s to Start Match Run...       \n", CLR_GREEN, CLR_RESET);
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
printf(" %s%-10s%s: %s%d %s\n", CLR_GREEN, players[i].name, CLR_RESET, CLR_YELLOW, players[i].total_stars, ICON_STAR);
if (players[i].total_stars > max_stars) {
max_stars = players[i].total_stars;
winner_idx = i;
}
}
printf("\n%s WINNER IS: %s! %s\n", CLR_YELLOW ICON_CROWN, players[winner_idx].name, ICON_CROWN CLR_RESET);
printMiiFace(winner_idx, "win");
printf("\n\nPress %sA%s to return to Main Menu...           \n", CLR_GREEN, CLR_RESET);
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
