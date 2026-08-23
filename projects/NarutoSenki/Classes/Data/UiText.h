#pragma once

#include <cstdint>
#include <cstring>

namespace UiText
{
inline const char *menuTitle(uint8_t id)
{
    switch (id)
    {
    case 0: return "NETWORK";
    case 1: return "TRAINING";
    case 2: return "EXIT";
    case 3: return "CREDITS";
    case 4: return "HARDCORE";
    default: return "";
    }
}

inline const char *modeTitle(uint8_t id)
{
    switch (id)
    {
    case 0: return "1 VS 1";
    case 1: return "3 VS 3";
    case 2: return "4 VS 4";
    case 3: return "HARDCORE";
    case 4: return "BOSS";
    case 5: return "CLONE";
    case 6: return "DEATHMATCH";
    case 7: return "RANDOM DEATHMATCH";
    default: return "";
    }
}

inline const char *gearName(uint8_t id)
{
    switch (id)
    {
    case 0: return "SPEED BOOTS";
    case 1: return "CHAKRA BAND";
    case 2: return "CHAKRA STAR";
    case 3: return "KUNAI TRAP";
    case 4: return "KUNAI";
    case 5: return "JUTSU BOOK";
    case 6: return "SUBSTITUTE";
    case 7: return "PILL";
    case 8: return "NINJA ARMOR";
    default: return "UNKNOWN GEAR";
    }
}

inline const char *loadingTip(uint8_t id)
{
    switch (id)
    {
    case 1: return "TIP: USE GEAR TO IMPROVE YOUR FIGHTER";
    case 2: return "TIP: COMBINE SKILLS FOR MORE DAMAGE";
    case 3: return "TIP: WATCH YOUR CHAKRA";
    default: return "LOADING...";
    }
}

inline const char *gearDescription(uint8_t id)
{
    switch (id)
    {
    case 0: return "SPEED +25%\nSTEALTH 3S";
    case 1: return "CHAKRA +25%";
    case 2: return "HITS RESTORE\nCHAKRA";
    case 3: return "TRAP ON HIT\nDEALS DMG";
    case 4: return "EXTRA DMG\nPIERCE ARMOR";
    case 5: return "CD -25%\nATTACK UP";
    case 6: return "ESCAPE DMG\nINVINCIBLE 1.5S";
    case 7: return "MORE USES\nRESTORE HP";
    case 8: return "MAX HP +6000\nDMG DOWN";
    default: return "";
    }
}

inline const char *common(const char *key)
{
    if (!key) return "";
    if (std::strcmp(key, "buy") == 0) return "BUY";
    if (std::strcmp(key, "resume") == 0) return "RESUME";
    if (std::strcmp(key, "back") == 0) return "BACK TO MENU";
    if (std::strcmp(key, "yes") == 0) return "YES";
    if (std::strcmp(key, "no") == 0) return "NO";
    if (std::strcmp(key, "pause") == 0) return "PAUSE";
    if (std::strcmp(key, "game_modes") == 0) return "GAME MODES";
    if (std::strcmp(key, "return") == 0) return "RETURN";
    if (std::strcmp(key, "exit") == 0) return "EXIT";
    return key;
}
} // namespace UiText
