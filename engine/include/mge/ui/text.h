#pragma once

// Text foundations (task 5.2/P11): UTF-8 decoding and v1 bidirectional
// handling. Hebrew is a right-to-left, non-connecting script, so correct
// basic rendering needs run reordering, not glyph shaping: in an RTL
// context, RTL runs read right-to-left while embedded Latin/digit runs keep
// their internal left-to-right order. That is what this implements; the
// full UAX#9 algorithm (nesting, explicit marks) is a planned upgrade and
// slots in behind the same function.

#include <cstddef>
#include <cstdint>

namespace mge {

// Decodes one UTF-8 codepoint; advances *cursor. Returns U+FFFD on error.
uint32_t utf8Next(const char** cursor);

// Number of codepoints in a UTF-8 string.
size_t utf8Length(const char* text);

bool isRtlCodepoint(uint32_t codepoint);  // Hebrew block (+ presentation forms)

// True if the string contains any RTL codepoint (used for direction
// detection when the language doesn't dictate it).
bool containsRtl(const char* text);

// Reorders codepoints for display. `outCapacity` codepoints max; returns the
// count written. If rtlBase is false the string passes through unchanged;
// if true, runs are laid out right-to-left with LTR/digit runs preserved.
size_t bidiReorder(const char* utf8, bool rtlBase, uint32_t* out, size_t outCapacity);

}  // namespace mge
