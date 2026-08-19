#pragma once

// The Codex design language (task 5.3, owner verdict: medieval,
// book-and-paper). Theme tokens are the single source of the engine UI's
// visual identity; games restyle by swapping the theme.

namespace mge {

struct Theme {
    // Palette
    float parchment[4];       // page ground
    float parchmentDeep[4];   // pressed / recessed surfaces
    float vellum[4];          // lighter raised surfaces
    float ink[4];             // primary text & rules
    float inkFaint[4];        // secondary text
    float crimson[4];         // heraldic accent (focus, health, seals)
    float gold[4];            // manuscript gold (highlights, coins)
    float leather[4];         // screen backdrop behind pages
    float shadow[4];          // soft drop shadow
    // Metrics
    float pad;                // base padding unit
    float buttonHeight;
    float borderThick;
};

inline Theme codexTheme() {
    Theme t{};
    auto set = [](float* c, float r, float g, float b, float a) {
        c[0] = r; c[1] = g; c[2] = b; c[3] = a;
    };
    set(t.parchment,     0.937f, 0.894f, 0.788f, 1.0f);  // #EFE4C9
    set(t.parchmentDeep, 0.867f, 0.808f, 0.671f, 1.0f);  // #DDCEAB
    set(t.vellum,        0.962f, 0.929f, 0.847f, 1.0f);  // #F5EDD8
    set(t.ink,           0.173f, 0.133f, 0.086f, 1.0f);  // #2C2216
    set(t.inkFaint,      0.475f, 0.412f, 0.322f, 1.0f);  // #796952
    set(t.crimson,       0.557f, 0.184f, 0.169f, 1.0f);  // #8E2F2B
    set(t.gold,          0.561f, 0.455f, 0.184f, 1.0f);  // #8F742F
    set(t.leather,       0.165f, 0.125f, 0.078f, 0.92f); // dark backdrop
    set(t.shadow,        0.0f,   0.0f,   0.0f,   0.35f);
    t.pad = 12.0f;
    t.buttonHeight = 56.0f;
    t.borderThick = 2.0f;
    return t;
}

}  // namespace mge
