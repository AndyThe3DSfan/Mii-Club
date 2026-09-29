#include <3ds.h>
#include <3ds/applets/miiselector.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

struct PartyMii {
    MiiData data{};
    char name[32]{};
    bool cpu = true;
    int score = 0;
};

static PartyMii players[4];

static bool chooseMii(int slot, bool playerControlled)
{
    MiiSelectorConf conf;
    MiiSelectorReturn ret;

    miiSelectorInit(&conf);
    miiSelectorSetTitle(&conf, playerControlled ? "Choose your Mii" : "Choose a CPU Mii");
    miiSelectorSetOptions(&conf, MIISELECTOR_CANCEL);

    std::memset(&ret, 0, sizeof(ret));
    miiSelectorLaunch(&conf, &ret);

    if (ret.no_mii_selected || !miiSelectorChecksumIsValid(&ret))
        return false;

    players[slot].data = ret.mii;
    players[slot].cpu = !playerControlled;
    players[slot].score = 0;
    miiSelectorReturnGetName(&ret, players[slot].name, sizeof(players[slot].name));
    return true;
}

static void printParty()
{
    consoleClear();
    printf("================================\n");
    printf("             MII CLUB\n");
    printf("================================\n\n");
    printf("Everyone is a CPU except YOU.\n\n");

    for (int i = 0; i < 4; ++i)
    {
        printf("%d. %s  %s\n",
               i + 1,
               players[i].name[0] ? players[i].name : "Mii",
               players[i].cpu ? "[CPU]" : "[YOU]");
    }

    printf("\nA = start minigame\n");
    printf("START = quit\n");
}

static void waitForA()
{
    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break;
        if (kDown & KEY_A)
            break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}

static void reactionGame()
{
    // Simple first prototype: tap A when the target appears.
    // CPU scores are generated locally; no network or server is used.
    consoleClear();
    printf("================================\n");
    printf("          REACTION RUSH\n");
    printf("================================\n\n");
    printf("Press A when GO appears!\n");
    printf("Get ready...\n");

    for (int i = 0; i < 60; ++i)
    {
        hidScanInput();
        if (hidKeysDown() & KEY_START) return;
        gspWaitForVBlank();
    }

    // Wait a random amount before GO.
    std::srand((unsigned)osGetTime());
    int delay = 60 + (std::rand() % 120);

    for (int i = 0; i < delay; ++i)
    {
        hidScanInput();
        if (hidKeysDown() & KEY_START) return;
        gspWaitForVBlank();
    }

    consoleClear();
    printf("\n\n\n              GO!\n");

    u64 start = osGetTime();
    bool pressed = false;

    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START)
            return;

        if (kDown & KEY_A)
        {
            u64 elapsed = osGetTime() - start;
            players[0].score = (elapsed < 200) ? 3 :
                               (elapsed < 400) ? 2 : 1;
            pressed = true;
            break;
        }

        gspWaitForVBlank();
    }

    if (!pressed)
        players[0].score = 0;

    // Offline CPU logic. Difficulty will be expanded later.
    for (int i = 1; i < 4; ++i)
        players[i].score = 1 + (std::rand() % 3);

    consoleClear();
    printf("================================\n");
    printf("             RESULTS\n");
    printf("================================\n\n");

    for (int i = 0; i < 4; ++i)
    {
        printf("%d. %s  +%d\n", i + 1, players[i].name, players[i].score);
    }

    printf("\nA = back to party\n");
    waitForA();
}

int main()
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    // Select the human player's Mii first.
    if (!chooseMii(0, true))
    {
        gfxExit();
        return 0;
    }

    // For this first prototype, choose three Miis that will be CPU-controlled.
    // The player never controls these Miis during gameplay.
    for (int i = 1; i < 4; ++i)
    {
        if (!chooseMii(i, false))
        {
            gfxExit();
            return 0;
        }
    }

    while (aptMainLoop())
    {
        printParty();
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START)
            break;

        if (kDown & KEY_A)
            reactionGame();

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
