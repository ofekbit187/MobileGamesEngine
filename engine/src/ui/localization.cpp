#include "mge/ui/localization.h"

namespace mge {

Localization::Localization() {
    // Engine-default strings for the built-in screen kit (task 5.5).
    struct Entry {
        const char* key;
        const char* en;
        const char* he;
    };
    static const Entry kDefaults[] = {
        {"menu.continue", "Continue", "המשך"},
        {"menu.new_game", "New Game", "משחק חדש"},
        {"menu.settings", "Settings", "הגדרות"},
        {"menu.built_on", "built on MobileGamesEngine",
         "מבוסס על MobileGamesEngine"},
        {"pause.title", "Paused", "הפסקה"},
        {"pause.resume", "Resume", "חזרה"},
        {"pause.quit", "Quit", "יציאה"},
        {"settings.language", "Language", "שפה"},
        {"settings.sound", "Sound", "צליל"},
        {"inv.title", "Backpack", "תרמיל"},
        {"inv.chest", "Chest", "תיבה"},
        {"item.apple", "Apple", "תפוח"},
        {"item.rope", "Rope", "חבל"},
        {"item.sword", "Sword", "חרב"},
        {"item.torch", "Torch", "לפיד"},
        {"item.coin", "Coin", "מטבע"},
    };
    for (const Entry& entry : kDefaults) {
        set(Language::English, entry.key, entry.en);
        set(Language::Hebrew, entry.key, entry.he);
    }
}

const char* Localization::get(const char* key) const {
    const auto& active = tables_[static_cast<size_t>(language_)];
    auto it = active.find(key);
    if (it != active.end()) return it->second.c_str();
    const auto& english = tables_[0];
    it = english.find(key);
    if (it != english.end()) return it->second.c_str();
    return key;
}

void Localization::set(Language language, const char* key, const char* text) {
    tables_[static_cast<size_t>(language)][key] = text;
}

}  // namespace mge
