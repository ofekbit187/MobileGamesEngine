#pragma once

// Localization (task 5.8, P11): UI references strings through keys; the
// active language decides text and reading direction. The engine ships
// English and Hebrew; games add languages by adding tables.

#include <cstdint>
#include <string>
#include <unordered_map>

namespace mge {

enum class Language : uint8_t { English = 0, Hebrew };

class Localization {
public:
    Localization();

    void setLanguage(Language language) { language_ = language; }
    Language language() const { return language_; }
    bool rtl() const { return language_ == Language::Hebrew; }

    // Returns the localized string, falling back to English, then to the key
    // itself (a visible fallback beats an invisible one).
    const char* get(const char* key) const;

    void set(Language language, const char* key, const char* text);

private:
    Language language_ = Language::English;
    std::unordered_map<std::string, std::string> tables_[2];
};

}  // namespace mge
