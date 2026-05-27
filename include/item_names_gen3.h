/* Gen 3 item names focused on HOLD items -- the things you'd actually
 * equip to a Pokémon. Verified against Bulbapedia + PKHeX. The full
 * Gen 3 item range is 0..376 but most of it is Pokéballs, key items,
 * TMs/HMs etc. that don't make sense to hold. Anything not in this
 * table renders as "Item #N". */
#ifndef POKEBRIDGE_ITEM_NAMES_GEN3_H
#define POKEBRIDGE_ITEM_NAMES_GEN3_H

static const char *const pb_item_names_gen3[377] = {
    [0]   = "(none)",

    /* Status berries / healing berries */
    [133] = "Cheri Berry",     [134] = "Chesto Berry",    [135] = "Pecha Berry",
    [136] = "Rawst Berry",     [137] = "Aspear Berry",    [138] = "Leppa Berry",
    [139] = "Oran Berry",      [140] = "Persim Berry",    [141] = "Lum Berry",
    [142] = "Sitrus Berry",    [143] = "Figy Berry",      [144] = "Wiki Berry",
    [145] = "Mago Berry",      [146] = "Aguav Berry",     [147] = "Iapapa Berry",
    [148] = "Razz Berry",      [149] = "Bluk Berry",      [150] = "Nanab Berry",
    [151] = "Wepear Berry",    [152] = "Pinap Berry",
    [153] = "Pomeg Berry",     [154] = "Kelpsy Berry",    [155] = "Qualot Berry",
    [156] = "Hondew Berry",    [157] = "Grepa Berry",     [158] = "Tamato Berry",
    [159] = "Cornn Berry",     [160] = "Magost Berry",    [161] = "Rabuta Berry",
    [162] = "Nomel Berry",     [163] = "Spelon Berry",    [164] = "Pamtre Berry",
    [165] = "Watmel Berry",    [166] = "Durin Berry",     [167] = "Belue Berry",
    [168] = "Liechi Berry",    [169] = "Ganlon Berry",    [170] = "Salac Berry",
    [171] = "Petaya Berry",    [172] = "Apicot Berry",    [173] = "Lansat Berry",
    [174] = "Starf Berry",     [175] = "Enigma Berry",

    /* Type-boost items */
    [180] = "Bright Powder",   [181] = "White Herb",      [182] = "Macho Brace",
    [183] = "Exp. Share",      [184] = "Quick Claw",      [185] = "Soothe Bell",
    [186] = "Mental Herb",     [187] = "Choice Band",     [188] = "King's Rock",
    [189] = "Silver Powder",   [190] = "Amulet Coin",     [191] = "Cleanse Tag",
    [192] = "Soul Dew",        [193] = "Deep Sea Tooth",  [194] = "Deep Sea Scale",
    [195] = "Smoke Ball",      [196] = "Everstone",       [197] = "Focus Band",
    [198] = "Lucky Egg",       [199] = "Scope Lens",      [200] = "Metal Coat",
    [201] = "Leftovers",       [202] = "Dragon Scale",    [203] = "Light Ball",
    [204] = "Soft Sand",       [205] = "Hard Stone",      [206] = "Miracle Seed",
    [207] = "Black Glasses",   [208] = "Black Belt",      [209] = "Magnet",
    [210] = "Mystic Water",    [211] = "Sharp Beak",      [212] = "Poison Barb",
    [213] = "NeverMeltIce",    [214] = "Spell Tag",       [215] = "TwistedSpoon",
    [216] = "Charcoal",        [217] = "Dragon Fang",     [218] = "Silk Scarf",
    [219] = "Up-Grade",        [220] = "Shell Bell",      [221] = "Sea Incense",
    [222] = "Lax Incense",     [223] = "Lucky Punch",     [224] = "Metal Powder",
    [225] = "Thick Club",      [226] = "Stick",
};

#endif
