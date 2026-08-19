#include "mge/ui/text.h"

namespace mge {

uint32_t utf8Next(const char** cursor) {
    const uint8_t* s = reinterpret_cast<const uint8_t*>(*cursor);
    if (s[0] == 0) return 0;
    uint32_t codepoint = 0xFFFD;
    int length = 1;
    if (s[0] < 0x80) {
        codepoint = s[0];
    } else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        codepoint = (static_cast<uint32_t>(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        length = 2;
    } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        codepoint = (static_cast<uint32_t>(s[0] & 0x0F) << 12) |
                    (static_cast<uint32_t>(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        length = 3;
    } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
               (s[3] & 0xC0) == 0x80) {
        codepoint = (static_cast<uint32_t>(s[0] & 0x07) << 18) |
                    (static_cast<uint32_t>(s[1] & 0x3F) << 12) |
                    (static_cast<uint32_t>(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        length = 4;
    }
    *cursor += length;
    return codepoint;
}

size_t utf8Length(const char* text) {
    size_t count = 0;
    while (utf8Next(&text) != 0) ++count;
    return count;
}

bool isRtlCodepoint(uint32_t cp) {
    return (cp >= 0x0590 && cp <= 0x05FF) ||  // Hebrew
           (cp >= 0xFB1D && cp <= 0xFB4F);    // Hebrew presentation forms
}

bool containsRtl(const char* text) {
    uint32_t cp;
    while ((cp = utf8Next(&text)) != 0) {
        if (isRtlCodepoint(cp)) return true;
    }
    return false;
}

namespace {
bool isNeutral(uint32_t cp) {
    // Spaces and common punctuation take the direction of their context.
    return cp == ' ' || cp == '-' || cp == ':' || cp == ',' || cp == '.' || cp == '!' ||
           cp == '?' || cp == '(' || cp == ')' || cp == '/' || cp == 0x2013 || cp == 0x2014;
}
}  // namespace

size_t bidiReorder(const char* utf8, bool rtlBase, uint32_t* out, size_t outCapacity) {
    // Decode.
    size_t count = 0;
    {
        const char* cursor = utf8;
        uint32_t cp;
        while ((cp = utf8Next(&cursor)) != 0 && count < outCapacity) out[count++] = cp;
    }
    if (!rtlBase || count == 0) return count;
    if (count > 512) return count;  // oversized: leave logical order

    // Classify: 1 = RTL, 0 = LTR, -1 = neutral (takes context).
    int8_t cls[512];
    for (size_t i = 0; i < count; ++i) {
        cls[i] = isRtlCodepoint(out[i]) ? 1 : (isNeutral(out[i]) ? -1 : 0);
    }
    // Resolve neutrals: same direction as both strong neighbors when they
    // agree, otherwise the base direction (RTL here).
    for (size_t i = 0; i < count; ++i) {
        if (cls[i] != -1) continue;
        int8_t prev = 1, next = 1;  // missing neighbor -> base (RTL)
        for (size_t k = i; k-- > 0;) {
            if (cls[k] != -1) { prev = cls[k]; break; }
        }
        size_t j = i;
        while (j < count && cls[j] == -1) ++j;
        if (j < count) next = cls[j];
        const int8_t resolved = (prev == next) ? prev : 1;
        for (size_t k = i; k < j; ++k) cls[k] = resolved;
        i = j;
    }
    // Group into runs of equal direction; visual order for an RTL base is
    // runs reversed, RTL runs also reversed internally.
    uint32_t visual[512];
    size_t v = 0;
    size_t runEnd = count;
    while (runEnd > 0) {
        size_t runBegin = runEnd - 1;
        while (runBegin > 0 && cls[runBegin - 1] == cls[runEnd - 1]) --runBegin;
        if (cls[runBegin] == 1) {
            for (size_t k = runEnd; k-- > runBegin;) visual[v++] = out[k];
        } else {
            for (size_t k = runBegin; k < runEnd; ++k) visual[v++] = out[k];
        }
        runEnd = runBegin;
    }
    for (size_t k = 0; k < v; ++k) out[k] = visual[k];
    return count;
}

}  // namespace mge
