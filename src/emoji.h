#pragma once
// Hand-crafted 5×7 emoji glyphs for the 8×32 WS2812B matrix.
// Each glyph is [row 0..6 top→bottom][col 0..4 left→right].
// CRGB(0,0,0) = off / transparent.
#include <FastLED.h>

#define EMOJI_W 5
#define EMOJI_H 7

struct EmojiGlyph {
    uint32_t cp;
    CRGB px[EMOJI_H][EMOJI_W];
};

// Color shorthand — undefined at end of file
#define __ CRGB(0,0,0)
#define RR CRGB(220,20,20)
#define OO CRGB(255,120,0)
#define YY CRGB(250,200,0)
#define GG CRGB(20,200,20)
#define BB CRGB(40,120,255)
#define PP CRGB(150,30,220)
#define WW CRGB(200,200,200)
#define SS CRGB(240,185,90)   // face/skin yellow
#define NN CRGB(100,60,15)    // brown
#define CC CRGB(0,200,220)    // cyan

static const EmojiGlyph EMOJI_TABLE[] = {

// ────────────────── HEARTS ──────────────────

// ❤️  U+2764  red heart
{ 0x2764, {{ __, RR, __, RR, __ },
            { RR, RR, RR, RR, RR },
            { RR, RR, RR, RR, RR },
            { RR, RR, RR, RR, RR },
            { __, RR, RR, RR, __ },
            { __, __, RR, __, __ },
            { __, __, __, __, __ }}},

// 🧡  U+1F9E1  orange heart
{ 0x1F9E1, {{ __, OO, __, OO, __ },
             { OO, OO, OO, OO, OO },
             { OO, OO, OO, OO, OO },
             { OO, OO, OO, OO, OO },
             { __, OO, OO, OO, __ },
             { __, __, OO, __, __ },
             { __, __, __, __, __ }}},

// 💛  U+1F49B  yellow heart
{ 0x1F49B, {{ __, YY, __, YY, __ },
             { YY, YY, YY, YY, YY },
             { YY, YY, YY, YY, YY },
             { YY, YY, YY, YY, YY },
             { __, YY, YY, YY, __ },
             { __, __, YY, __, __ },
             { __, __, __, __, __ }}},

// 💚  U+1F49A  green heart
{ 0x1F49A, {{ __, GG, __, GG, __ },
             { GG, GG, GG, GG, GG },
             { GG, GG, GG, GG, GG },
             { GG, GG, GG, GG, GG },
             { __, GG, GG, GG, __ },
             { __, __, GG, __, __ },
             { __, __, __, __, __ }}},

// 💙  U+1F499  blue heart
{ 0x1F499, {{ __, BB, __, BB, __ },
             { BB, BB, BB, BB, BB },
             { BB, BB, BB, BB, BB },
             { BB, BB, BB, BB, BB },
             { __, BB, BB, BB, __ },
             { __, __, BB, __, __ },
             { __, __, __, __, __ }}},

// 💜  U+1F49C  purple heart
{ 0x1F49C, {{ __, PP, __, PP, __ },
             { PP, PP, PP, PP, PP },
             { PP, PP, PP, PP, PP },
             { PP, PP, PP, PP, PP },
             { __, PP, PP, PP, __ },
             { __, __, PP, __, __ },
             { __, __, __, __, __ }}},

// ────────────────── FACES ──────────────────

// 😊  U+1F60A  smiley (also ☺️ U+263A)
{ 0x1F60A, {{ __, SS, SS, SS, __ },
             { SS, SS, SS, SS, SS },
             { SS, __, SS, __, SS },   // eyes = dark gaps
             { SS, SS, SS, SS, SS },
             { SS, SS, __, SS, SS },   // smile corners
             { SS, __, __, __, SS },
             { __, SS, SS, SS, __ }}},

{ 0x263A,  {{ __, SS, SS, SS, __ },
             { SS, SS, SS, SS, SS },
             { SS, __, SS, __, SS },
             { SS, SS, SS, SS, SS },
             { SS, SS, __, SS, SS },
             { SS, __, __, __, SS },
             { __, SS, SS, SS, __ }}},

// 😂  U+1F602  laughing/tears of joy — eyes squint, mouth wide open
{ 0x1F602, {{ __, SS, SS, SS, __ },
             { SS, SS, SS, SS, SS },
             { SS, __, SS, __, SS },
             { SS, SS, SS, SS, SS },
             { SS, __, __, __, SS },   // wide open grin
             { SS, __, __, __, SS },
             { __, SS, SS, SS, __ }}},

// 😢  U+1F622  sad
{ 0x1F622, {{ __, SS, SS, SS, __ },
             { SS, SS, SS, SS, SS },
             { SS, __, SS, __, SS },   // eyes
             { SS, SS, SS, SS, SS },
             { SS, __, SS, __, SS },   // frown
             { SS, SS, __, SS, SS },
             { __, SS, SS, SS, __ }}},

// 😠  U+1F620  angry — red V-brows
{ 0x1F620, {{ RR, SS, __, SS, RR },
             { __, SS, SS, SS, __ },
             { SS, __, SS, __, SS },
             { SS, SS, SS, SS, SS },
             { SS, __, SS, __, SS },
             { SS, SS, __, SS, SS },
             { __, SS, SS, SS, __ }}},

// ────────────────── NATURE & FIRE ──────────────────

// 🔥  U+1F525  fire — yellow tip, orange body, red base
{ 0x1F525, {{ __, __, YY, __, __ },
             { __, YY, YY, YY, __ },
             { __, OO, YY, OO, __ },
             { OO, OO, OO, OO, __ },
             { RR, OO, OO, OO, RR },
             { __, RR, RR, RR, __ },
             { __, __, __, __, __ }}},

// ☀️  U+2600  sun — yellow with rays
{ 0x2600,  {{ YY, __, YY, __, YY },
             { __, YY, YY, YY, __ },
             { YY, YY, YY, YY, YY },
             { __, YY, YY, YY, __ },
             { YY, __, YY, __, YY },
             { __, __, __, __, __ },
             { __, __, __, __, __ }}},

// 🌧️  U+1F327  rain cloud — gray cloud, blue drops
{ 0x1F327, {{ __, WW, WW, WW, __ },
             { WW, WW, WW, WW, WW },
             { WW, WW, WW, WW, WW },
             { __, WW, WW, WW, __ },
             { BB, __, BB, __, BB },
             { BB, __, BB, __, BB },
             { __, __, __, __, __ }}},

// ────────────────── SYMBOLS ──────────────────

// ⭐  U+2B50  star
{ 0x2B50,  {{ __, __, YY, __, __ },
             { __, YY, YY, YY, __ },
             { YY, YY, YY, YY, YY },
             { __, YY, YY, YY, __ },
             { YY, __, __, __, YY },
             { __, __, __, __, __ },
             { __, __, __, __, __ }}},

// 🌟  U+1F31F  glowing star — rays at corners
{ 0x1F31F, {{ __, __, YY, __, __ },
             { YY, __, YY, __, YY },
             { __, YY, YY, YY, __ },
             { YY, YY, YY, YY, YY },
             { __, YY, YY, YY, __ },
             { YY, __, YY, __, YY },
             { __, __, YY, __, __ }}},

// ⚠️  U+26A0  warning — yellow triangle, exclamation dot below
{ 0x26A0,  {{ __, __, YY, __, __ },
             { __, YY, YY, YY, __ },
             { __, YY, __, YY, __ },
             { YY, YY, YY, YY, YY },
             { YY, YY, __, YY, YY },
             { __, __, __, __, __ },
             { __, __, YY, __, __ }}},

// ✅  U+2705  check mark — green
{ 0x2705,  {{ __, __, __, __, __ },
             { __, __, __, __, GG },
             { __, __, __, GG, GG },
             { GG, __, GG, GG, __ },
             { GG, GG, GG, __, __ },
             { __, GG, __, __, __ },
             { __, __, __, __, __ }}},

// ❌  U+274C  X — red
{ 0x274C,  {{ RR, __, __, __, RR },
             { RR, RR, __, RR, RR },
             { __, RR, RR, RR, __ },
             { __, __, RR, __, __ },
             { __, RR, RR, RR, __ },
             { RR, RR, __, RR, RR },
             { RR, __, __, __, RR }}},

// 💯  U+1F4AF  100 — red digits, underline
{ 0x1F4AF, {{ RR, __, RR, __, RR },
             { RR, __, RR, __, RR },
             { RR, __, RR, __, RR },
             { RR, __, RR, __, RR },
             { RR, __, RR, __, RR },
             { __, __, __, __, __ },
             { RR, RR, RR, RR, RR }}},

// ────────────────── HANDS ──────────────────

// 👍  U+1F44D  thumbs up
{ 0x1F44D, {{ __, __, SS, SS, __ },
             { __, SS, SS, SS, __ },
             { SS, SS, SS, SS, SS },
             { __, SS, SS, SS, SS },
             { __, SS, SS, SS, SS },
             { __, SS, SS, SS, SS },
             { __, __, __, __, __ }}},

// 👎  U+1F44E  thumbs down
{ 0x1F44E, {{ __, SS, SS, SS, SS },
             { __, SS, SS, SS, SS },
             { __, SS, SS, SS, SS },
             { SS, SS, SS, SS, SS },
             { __, SS, SS, SS, __ },
             { __, __, SS, SS, __ },
             { __, __, __, __, __ }}},

// 👋  U+1F44B  waving hand
{ 0x1F44B, {{ __, SS, __, SS, __ },
             { SS, SS, SS, SS, __ },
             { SS, SS, SS, SS, SS },
             { SS, SS, SS, SS, SS },
             { __, SS, SS, SS, SS },
             { __, __, SS, SS, __ },
             { __, __, __, __, __ }}},

// 🙏  U+1F64F  folded hands / please
{ 0x1F64F, {{ __, __, SS, __, __ },
             { __, SS, SS, SS, __ },
             { SS, SS, SS, SS, SS },
             { SS, SS, SS, SS, SS },
             { __, SS, SS, SS, __ },
             { __, __, SS, __, __ },
             { __, __, __, __, __ }}},

// ────────────────── COLORED CIRCLES ──────────────────

// 🔴  U+1F534  red circle
{ 0x1F534, {{ __, RR, RR, RR, __ },
             { RR, RR, RR, RR, RR },
             { RR, RR, RR, RR, RR },
             { RR, RR, RR, RR, RR },
             { __, RR, RR, RR, __ },
             { __, __, __, __, __ },
             { __, __, __, __, __ }}},

// 🟠  U+1F7E0  orange circle
{ 0x1F7E0, {{ __, OO, OO, OO, __ },
             { OO, OO, OO, OO, OO },
             { OO, OO, OO, OO, OO },
             { OO, OO, OO, OO, OO },
             { __, OO, OO, OO, __ },
             { __, __, __, __, __ },
             { __, __, __, __, __ }}},

// 🟡  U+1F7E1  yellow circle
{ 0x1F7E1, {{ __, YY, YY, YY, __ },
             { YY, YY, YY, YY, YY },
             { YY, YY, YY, YY, YY },
             { YY, YY, YY, YY, YY },
             { __, YY, YY, YY, __ },
             { __, __, __, __, __ },
             { __, __, __, __, __ }}},

// 🟢  U+1F7E2  green circle
{ 0x1F7E2, {{ __, GG, GG, GG, __ },
             { GG, GG, GG, GG, GG },
             { GG, GG, GG, GG, GG },
             { GG, GG, GG, GG, GG },
             { __, GG, GG, GG, __ },
             { __, __, __, __, __ },
             { __, __, __, __, __ }}},

// 🔵  U+1F535  blue circle
{ 0x1F535, {{ __, BB, BB, BB, __ },
             { BB, BB, BB, BB, BB },
             { BB, BB, BB, BB, BB },
             { BB, BB, BB, BB, BB },
             { __, BB, BB, BB, __ },
             { __, __, __, __, __ },
             { __, __, __, __, __ }}},

// 🟣  U+1F7E3  purple circle
{ 0x1F7E3, {{ __, PP, PP, PP, __ },
             { PP, PP, PP, PP, PP },
             { PP, PP, PP, PP, PP },
             { PP, PP, PP, PP, PP },
             { __, PP, PP, PP, __ },
             { __, __, __, __, __ },
             { __, __, __, __, __ }}},

// ────────────────── RADIO / MESH ──────────────────

// 📡  U+1F4E1  satellite dish
{ 0x1F4E1, {{ __, WW, WW, WW, __ },
             { WW, __, __, __, WW },
             { WW, __, WW, __, WW },
             { __, WW, WW, WW, __ },
             { __, __, WW, __, __ },
             { __, WW, WW, WW, __ },
             { __, __, __, __, __ }}},

// 📻  U+1F4FB  radio — box with antenna
{ 0x1F4FB, {{ __, __, WW, __, __ },
             { WW, WW, WW, WW, WW },
             { WW, __, WW, __, WW },
             { WW, WW, WW, WW, WW },
             { WW, WW, WW, WW, WW },
             { __, __, __, __, __ },
             { __, __, __, __, __ }}},

// 🔋  U+1F50B  battery — white shell, green fill
{ 0x1F50B, {{ __, WW, WW, WW, __ },
             { WW, WW, WW, WW, WW },
             { WW, GG, GG, GG, WW },
             { WW, GG, GG, GG, WW },
             { WW, GG, GG, GG, WW },
             { WW, WW, WW, WW, WW },
             { __, __, __, __, __ }}},

// ────────────────── ALERTS ──────────────────

// 🆘  U+1F198  SOS — red box, S silhouette in white
{ 0x1F198, {{ RR, RR, RR, RR, RR },
             { RR, WW, WW, WW, RR },
             { RR, WW, RR, RR, RR },
             { RR, WW, WW, WW, RR },
             { RR, RR, RR, WW, RR },
             { RR, WW, WW, WW, RR },
             { RR, RR, RR, RR, RR }}},

// 🛑  U+1F6D1  stop sign — red octagon
{ 0x1F6D1, {{ __, RR, RR, RR, __ },
             { RR, RR, RR, RR, RR },
             { RR, __, WW, __, RR },
             { RR, WW, WW, WW, RR },
             { RR, __, WW, __, RR },
             { RR, RR, RR, RR, RR },
             { __, RR, RR, RR, __ }}},

// 📍  U+1F4CD  location pin — red
{ 0x1F4CD, {{ __, RR, RR, RR, __ },
             { RR, RR, RR, RR, RR },
             { RR, RR, RR, RR, RR },
             { __, RR, RR, RR, __ },
             { __, __, RR, __, __ },
             { __, __, RR, __, __ },
             { __, __, __, __, __ }}},

// ────────────────── MISC ──────────────────

// 🎉  U+1F389  party popper — colorful confetti
{ 0x1F389, {{ __, __, __, PP, YY },
             { __, __, GG, RR, PP },
             { __, YY, __, __, RR },
             { __, OO, YY, __, __ },
             { OO, OO, OO, __, __ },
             { OO, OO, __, __, __ },
             { __, __, __, __, __ }}},

// 🌡️  U+1F321  thermometer — white tube, red mercury
{ 0x1F321, {{ __, __, WW, __, __ },
             { __, WW, __, WW, __ },
             { __, WW, RR, WW, __ },
             { __, WW, RR, WW, __ },
             { WW, WW, RR, WW, WW },
             { WW, WW, RR, WW, WW },
             { __, WW, WW, WW, __ }}},

};

static const int EMOJI_COUNT = (int)(sizeof(EMOJI_TABLE) / sizeof(EMOJI_TABLE[0]));

static const EmojiGlyph *find_emoji(uint32_t cp) {
    for (int i = 0; i < EMOJI_COUNT; i++)
        if (EMOJI_TABLE[i].cp == cp) return &EMOJI_TABLE[i];
    return nullptr;
}

#undef __
#undef RR
#undef OO
#undef YY
#undef GG
#undef BB
#undef PP
#undef WW
#undef SS
#undef NN
#undef CC
