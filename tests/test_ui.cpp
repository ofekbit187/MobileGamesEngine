// Phase 5 tests: UTF-8/bidi, localization, font atlas, widget interaction
// and input routing, RTL mirroring, collection binding.

#include <cstring>

#include "mge/ui/ui.h"
#include "test_framework.h"

using namespace mge;

MGE_TEST(utf8_decode) {
    const char* text = "a\xD7\x90z";  // a, aleph (U+05D0), z
    const char* cursor = text;
    MGE_CHECK(utf8Next(&cursor) == 'a');
    MGE_CHECK(utf8Next(&cursor) == 0x05D0);
    MGE_CHECK(utf8Next(&cursor) == 'z');
    MGE_CHECK(utf8Next(&cursor) == 0);
    MGE_CHECK(utf8Length(text) == 3);
    MGE_CHECK(containsRtl(text));
    MGE_CHECK(!containsRtl("hello"));
}

MGE_TEST(bidi_reorder_rules) {
    uint32_t out[64];

    // Pure LTR with LTR base: unchanged.
    size_t n = bidiReorder("abc", false, out, 64);
    MGE_CHECK(n == 3 && out[0] == 'a' && out[2] == 'c');

    // Pure RTL (Hebrew "shalom" letters) with RTL base: reversed for display.
    // Logical: shin(5E9) lamed(5DC) vav(5D5) final-mem(5DD)
    n = bidiReorder("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D", true, out, 64);
    MGE_CHECK(n == 4);
    MGE_CHECK(out[0] == 0x05DD && out[3] == 0x05E9);  // visual: mem first

    // Mixed: Hebrew then Latin brand name, RTL base. The Latin run keeps its
    // internal order but moves to the visual left.
    // Logical: aleph bet space M G E
    n = bidiReorder("\xD7\x90\xD7\x91 MGE", true, out, 64);
    MGE_CHECK(n == 6);
    MGE_CHECK(out[0] == 'M' && out[1] == 'G' && out[2] == 'E');  // LTR run intact, leftmost
    MGE_CHECK(out[4] == 0x05D1 && out[5] == 0x05D0);             // Hebrew reversed, rightmost
}

MGE_TEST(localization_lookup_and_fallback) {
    Localization strings;
    MGE_CHECK(strcmp(strings.get("menu.continue"), "Continue") == 0);
    MGE_CHECK(!strings.rtl());

    strings.setLanguage(Language::Hebrew);
    MGE_CHECK(strings.rtl());
    MGE_CHECK(strcmp(strings.get("menu.continue"), "המשך") == 0);
    // Missing key: visible fallback.
    MGE_CHECK(strcmp(strings.get("no.such.key"), "no.such.key") == 0);
}

MGE_TEST(font_atlas_bakes_latin_and_hebrew) {
    FontAtlas font;
    MGE_CHECK(font.bakeFromFile("/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf", 32.0f));
    MGE_CHECK(font.glyph('A') != nullptr);
    MGE_CHECK(font.glyph(0x05D0) != nullptr);  // aleph
    MGE_CHECK(font.glyph('A')->advance > 0);
    MGE_CHECK(font.lineHeight() > 0);
    // The reserved white texel for solid quads.
    MGE_CHECK(font.pixels()[0] == 255);
}

namespace {
TouchEvent touch(int32_t id, TouchAction action, float x, float y, int64_t t = 0) {
    TouchEvent e;
    e.pointerId = id;
    e.action = action;
    e.x = x;
    e.y = y;
    e.timestampNs = t;
    return e;
}
}  // namespace

MGE_TEST(ui_button_click_and_routing) {
    FontAtlas font;
    MGE_CHECK(font.bakeFromFile("/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf", 32.0f));
    Localization strings;
    Ui ui;
    MGE_CHECK(ui.init(&font, codexTheme(), &strings));

    auto frame = [&](bool* clicked) {
        ui.beginFrame(1280, 720);
        const bool c = ui.button(1, {100, 100, 200, 60}, "menu.continue");
        if (clicked != nullptr) *clicked = c;
    };

    frame(nullptr);  // establish interactive rects

    // Touch outside any widget: NOT consumed (falls through to gameplay).
    MGE_CHECK(!ui.handleTouch(touch(0, TouchAction::Down, 700, 400)));

    // Press + release on the button: consumed, and the widget reports a click.
    MGE_CHECK(ui.handleTouch(touch(0, TouchAction::Down, 150, 130)));
    MGE_CHECK(ui.handleTouch(touch(0, TouchAction::Up, 160, 130)));
    bool clicked = false;
    frame(&clicked);
    MGE_CHECK(clicked);

    // Press on the button, drag off, release: consumed but no click.
    MGE_CHECK(ui.handleTouch(touch(0, TouchAction::Down, 150, 130)));
    MGE_CHECK(ui.handleTouch(touch(0, TouchAction::Up, 600, 500)));
    frame(&clicked);
    MGE_CHECK(!clicked);

    // Drawing happened (quads were emitted).
    MGE_CHECK(ui.drawList().quadCount() > 0);
}

MGE_TEST(ui_rtl_mirroring) {
    FontAtlas font;
    MGE_CHECK(font.bakeFromFile("/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf", 32.0f));
    Localization strings;
    Ui ui;
    MGE_CHECK(ui.init(&font, codexTheme(), &strings));
    ui.beginFrame(1000, 500);

    const UiRect logical{100, 50, 200, 60};
    const UiRect ltr = ui.place(logical);
    MGE_CHECK_NEAR(ltr.x, 100.0f, 1e-5);

    strings.setLanguage(Language::Hebrew);
    const UiRect rtl = ui.place(logical);
    MGE_CHECK_NEAR(rtl.x, 1000.0f - 100.0f - 200.0f, 1e-5);  // mirrored
    MGE_CHECK_NEAR(rtl.y, 50.0f, 1e-5);                      // vertical unchanged
}

MGE_TEST(collection_binding) {
    ItemCollection backpack;
    MGE_CHECK(backpack.add({assetIdFromName("item/apple"), "item.apple", 3, {0.8f, 0.2f, 0.2f, 1}}));
    MGE_CHECK(backpack.add({assetIdFromName("item/rope"), "item.rope", 1, {0.6f, 0.5f, 0.3f, 1}}));
    // Stacking merges same-asset items.
    MGE_CHECK(backpack.add({assetIdFromName("item/apple"), "item.apple", 2, {0.8f, 0.2f, 0.2f, 1}}));
    MGE_CHECK(backpack.size() == 2);
    MGE_CHECK(backpack.at(0)->count == 5);

    CollectionRegistry registry;
    MGE_CHECK(registry.add("player.backpack", &backpack));
    MGE_CHECK(registry.find("player.backpack") == &backpack);
    MGE_CHECK(registry.find("no.such.collection") == nullptr);  // normal answer

    // Bound view renders slots and reacts to slot taps.
    FontAtlas font;
    MGE_CHECK(font.bakeFromFile("/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf", 32.0f));
    Localization strings;
    Ui ui;
    MGE_CHECK(ui.init(&font, codexTheme(), &strings));
    int selected = -1;
    ui.beginFrame(1280, 720);
    ui.collectionView(100, {100, 100, 400, 400}, "inv.title", *registry.find("player.backpack"),
                      5, selected);
    const size_t quads = ui.drawList().quadCount();
    MGE_CHECK(quads > 40);  // panel + title + grid of slots

    // Tap the first slot (top-left of the grid area).
    MGE_CHECK(ui.handleTouch(touch(0, TouchAction::Down, 130, 160)));
    MGE_CHECK(ui.handleTouch(touch(0, TouchAction::Up, 130, 160)));
    ui.beginFrame(1280, 720);
    ui.collectionView(100, {100, 100, 400, 400}, "inv.title", backpack, 5, selected);
    MGE_CHECK(selected == 0);
}
