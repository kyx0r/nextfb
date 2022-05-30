/* unicode glyph intervals to include */
static int chars[][2] = {
	{0x0000, 0x007f},	/* Basic Latin */
	{0x0080, 0x00ff},	/* Latin-1 Supplement */
	{0x0100, 0x017f},	/* Latin Extended-A */
	{0x0180, 0x024f},	/* Latin Extended-B */
	{0x0250, 0x02af},	/* IPA Extensions */
	{0x02b0, 0x02ff},	/* Spacing Modifier Letters */
	{0x0300, 0x036f},	/* Combining Diacritical Marks */
	{0x0370, 0x03ff},	/* Greek and Coptic */
	{0x0400, 0x04ff},	/* Cyrillic */
	{0x0500, 0x052f},	/* Cyrillic Supplement */
	{0x0530, 0x058f},	/* Armenian */
	{0x0590, 0x05ff},	/* Hebrew */
	{0x0600, 0x06ff},	/* Arabic */
	{0x0700, 0x074f},	/* Syriac */
	{0x0750, 0x077f},	/* Arabic Supplement */
	{0x0780, 0x07bf},	/* Thaana */
	{0x07c0, 0x07ff},	/* NKo */
	{0x0800, 0x083f},	/* Samaritan */
	{0x0900, 0x097f},	/* Devanagari */
	{0x0980, 0x09ff},	/* Bengali */
	{0x0a00, 0x0a7f},	/* Gurmukhi */
	{0x0a80, 0x0aff},	/* Gujarati */
	{0x0b00, 0x0b7f},	/* Oriya */
	{0x0b80, 0x0bff},	/* Tamil */
	{0x0c00, 0x0c7f},	/* Telugu */
	{0x0c80, 0x0cff},	/* Kannada */
	{0x0d00, 0x0d7f},	/* Malayalam */
	{0x0d80, 0x0dff},	/* Sinhala */
	{0x0e00, 0x0e7f},	/* Thai */
	{0x0e80, 0x0eff},	/* Lao */
	{0x0f00, 0x0fff},	/* Tibetan */
	{0x1000, 0x109f},	/* Myanmar */
	{0x10a0, 0x10ff},	/* Georgian */
	{0x1100, 0x11ff},	/* Hangul Jamo */
	{0x1200, 0x137f},	/* Ethiopic */
	{0x1380, 0x139f},	/* Ethiopic Supplement */
	{0x13a0, 0x13ff},	/* Cherokee */
	{0x1400, 0x167f},	/* Unified Canadian Aboriginal Syllabics */
	{0x1680, 0x169f},	/* Ogham */
	{0x16a0, 0x16ff},	/* Runic */
	{0x1700, 0x171f},	/* Tagalog */
	{0x1720, 0x173f},	/* Hanunoo */
	{0x1740, 0x175f},	/* Buhid */
	{0x1760, 0x177f},	/* Tagbanwa */
	{0x1780, 0x17ff},	/* Khmer */
	{0x1800, 0x18af},	/* Mongolian */
	{0x18b0, 0x18ff},	/* Unified Canadian Aboriginal Syllabics Extended */
	{0x1900, 0x194f},	/* Limbu */
	{0x1950, 0x197f},	/* Tai Le */
	{0x1980, 0x19df},	/* New Tai Lue */
	{0x19e0, 0x19ff},	/* Khmer Symbols */
	{0x1a00, 0x1a1f},	/* Buginese */
	{0x1a20, 0x1aaf},	/* Tai Tham */
	{0x1b00, 0x1b7f},	/* Balinese */
	{0x1b80, 0x1bbf},	/* Sundanese */
	{0x1c00, 0x1c4f},	/* Lepcha */
	{0x1c50, 0x1c7f},	/* Ol Chiki */
	{0x1cd0, 0x1cff},	/* Vedic Extensions */
	{0x1d00, 0x1d7f},	/* Phonetic Extensions */
	{0x1d80, 0x1dbf},	/* Phonetic Extensions Supplement */
	{0x1dc0, 0x1dff},	/* Combining Diacritical Marks Supplement */
	{0x1e00, 0x1eff},	/* Latin Extended Additional */
	{0x1f00, 0x1fff},	/* Greek Extended */
	{0x2000, 0x206f},	/* General Punctuation */
	{0x2070, 0x209f},	/* Superscripts and Subscripts */
	{0x20a0, 0x20cf},	/* Currency Symbols */
	{0x20d0, 0x20ff},	/* Combining Diacritical Marks for Symbols */
	{0x2100, 0x214f},	/* Letterlike Symbols */
	{0x2150, 0x218f},	/* Number Forms */
	{0x2190, 0x21ff},	/* Arrows */
	{0x2200, 0x22ff},	/* Mathematical Operators */
	{0x2300, 0x23ff},	/* Miscellaneous Technical */
	{0x2400, 0x243f},	/* Control Pictures */
	{0x2440, 0x245f},	/* Optical Character Recognition */
	{0x2460, 0x24ff},	/* Enclosed Alphanumerics */
	{0x2500, 0x257f},	/* Box Drawing */
	{0x2580, 0x259f},	/* Block Elements */
	{0x25a0, 0x25ff},	/* Geometric Shapes */
	{0x2600, 0x26ff},	/* Miscellaneous Symbols */
	{0x2700, 0x27bf},	/* Dingbats */
	{0x27c0, 0x27ef},	/* Miscellaneous Mathematical Symbols-A */
	{0x27f0, 0x27ff},	/* Supplemental Arrows-A */
	{0x2800, 0x28ff},	/* Braille Patterns */
	{0x2900, 0x297f},	/* Supplemental Arrows-B */
	{0x2980, 0x29ff},	/* Miscellaneous Mathematical Symbols-B */
	{0x2a00, 0x2aff},	/* Supplemental Mathematical Operators */
	{0x2b00, 0x2bff},	/* Miscellaneous Symbols and Arrows */
	{0x2c00, 0x2c5f},	/* Glagolitic */
	{0x2c60, 0x2c7f},	/* Latin Extended-C */
	{0x2c80, 0x2cff},	/* Coptic */
	{0x2d00, 0x2d2f},	/* Georgian Supplement */
	{0x2d30, 0x2d7f},	/* Tifinagh */
	{0x2d80, 0x2ddf},	/* Ethiopic Extended */
	{0x2de0, 0x2dff},	/* Cyrillic Extended-A */
	{0x2e00, 0x2e7f},	/* Supplemental Punctuation */
	{0x2e80, 0x2eff},	/* CJK Radicals Supplement */
	{0x2f00, 0x2fdf},	/* Kangxi Radicals */
	{0x2ff0, 0x2fff},	/* Ideographic Description Characters */
	{0x3000, 0x303f},	/* CJK Symbols and Punctuation */
	{0x3040, 0x309f},	/* Hiragana */
	{0x30a0, 0x30ff},	/* Katakana */
	{0x3100, 0x312f},	/* Bopomofo */
	{0x3130, 0x318f},	/* Hangul Compatibility Jamo */
	{0x3190, 0x319f},	/* Kanbun */
	{0x31a0, 0x31bf},	/* Bopomofo Extended */
	{0x31c0, 0x31ef},	/* CJK Strokes */
	{0x31f0, 0x31ff},	/* Katakana Phonetic Extensions */
	{0x3200, 0x32ff},	/* Enclosed CJK Letters and Months */
	{0x3300, 0x33ff},	/* CJK Compatibility */
	{0x3400, 0x9fff},	/* CJK ... */
	{0xa000, 0xa48f},	/* Yi Syllables */
	{0xa490, 0xa4cf},	/* Yi Radicals */
	{0xa4d0, 0xa4ff},	/* Lisu */
	{0xa500, 0xa63f},	/* Vai */
	{0xa640, 0xa69f},	/* Cyrillic Extended-B */
	{0xa6a0, 0xa6ff},	/* Bamum */
	{0xa700, 0xa71f},	/* Modifier Tone Letters */
	{0xa720, 0xa7ff},	/* Latin Extended-D */
	{0xa800, 0xa82f},	/* Syloti Nagri */
	{0xa830, 0xa83f},	/* Common Indic Number Forms */
	{0xa840, 0xa87f},	/* Phags-pa */
	{0xa880, 0xa8df},	/* Saurashtra */
	{0xa8e0, 0xa8ff},	/* Devanagari Extended */
	{0xa900, 0xa92f},	/* Kayah Li */
	{0xa930, 0xa95f},	/* Rejang */
	{0xa960, 0xa97f},	/* Hangul Jamo Extended-A */
	{0xa980, 0xa9df},	/* Javanese */
	{0xaa00, 0xaa5f},	/* Cham */
	{0xaa60, 0xaa7f},	/* Myanmar Extended-A */
	{0xac00, 0xd7ff},	/* Hangul ... */
	{0xd800, 0xdb7f},	/* High Surrogates */
	{0xdc00, 0xdfff},	/* Low Surrogates */
	{0xf900, 0xfaff},	/* CJK Compatibility Ideographs */
	{0xfb00, 0xfb4f},	/* Alphabetic Presentation Forms */
	{0xfb50, 0xfdff},	/* Arabic Presentation Forms-A */
	{0xfe00, 0xfe0f},	/* Variation Selectors */
	{0xfe10, 0xfe1f},	/* Vertical Forms */
	{0xfe20, 0xfe2f},	/* Combining Half Marks */
	{0xfe30, 0xfe4f},	/* CJK Compatibility Forms */
	{0xfe50, 0xfe6f},	/* Small Form Variants */
	{0xfe70, 0xfeff},	/* Arabic Presentation Forms-B */
	{0xff00, 0xffef},	/* Halfwidth and Fullwidth Forms */
	{0xfff0, 0xffff},	/* Specials */
};
