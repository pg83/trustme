#include <std/sys/types.h>
#include "unicode_nfc.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
    struct UnicodeCombining {
        u32 codepoint;
        u8 cls;
    };

    struct UnicodeDecomposition {
        u32 codepoint;
        u32 first;
        /// Zero for a singleton decomposition.
        u32 second;
    };

    struct UnicodeComposition {
        u32 first;
        u32 second;
        u32 composed;
    };

#include "unicode_nfc_tables.inc"

    // Hangul syllables are decomposed and composed arithmetically, not from a
    // table.
    constexpr u32 HANGUL_S_BASE = 0xAC00;
    constexpr u32 HANGUL_L_BASE = 0x1100;
    constexpr u32 HANGUL_V_BASE = 0x1161;
    constexpr u32 HANGUL_T_BASE = 0x11A7;
    constexpr u32 HANGUL_L_COUNT = 19;
    constexpr u32 HANGUL_V_COUNT = 21;
    constexpr u32 HANGUL_T_COUNT = 28;
    constexpr u32 HANGUL_N_COUNT = HANGUL_V_COUNT * HANGUL_T_COUNT;
    constexpr u32 HANGUL_S_COUNT = HANGUL_L_COUNT * HANGUL_N_COUNT;

    u8 combiningClass(u32 codepoint) {
        const auto* found = ::std::lower_bound(::std::begin(COMBINING), ::std::end(COMBINING), codepoint,
            [](const UnicodeCombining& entry, u32 value) {
                return entry.codepoint < value;
            });
        return found != ::std::end(COMBINING) && found->codepoint == codepoint ? found->cls : 0;
    }

    const UnicodeDecomposition* decompositionOf(u32 codepoint) {
        const auto* found = ::std::lower_bound(::std::begin(DECOMPOSITIONS), ::std::end(DECOMPOSITIONS), codepoint,
            [](const UnicodeDecomposition& entry, u32 value) {
                return entry.codepoint < value;
            });
        return found != ::std::end(DECOMPOSITIONS) && found->codepoint == codepoint ? found : nullptr;
    }

    u32 composedPair(u32 first, u32 second) {
        if (HANGUL_L_BASE <= first && first < HANGUL_L_BASE + HANGUL_L_COUNT && HANGUL_V_BASE <= second
            && second < HANGUL_V_BASE + HANGUL_V_COUNT) {
            return HANGUL_S_BASE + ((first - HANGUL_L_BASE) * HANGUL_V_COUNT + (second - HANGUL_V_BASE)) * HANGUL_T_COUNT;
        }
        if (HANGUL_S_BASE <= first && first < HANGUL_S_BASE + HANGUL_S_COUNT && (first - HANGUL_S_BASE) % HANGUL_T_COUNT == 0
            && HANGUL_T_BASE < second && second < HANGUL_T_BASE + HANGUL_T_COUNT) {
            return first + (second - HANGUL_T_BASE);
        }
        const auto* found = ::std::lower_bound(::std::begin(COMPOSITIONS), ::std::end(COMPOSITIONS), first,
            [](const UnicodeComposition& entry, u32 value) {
                return entry.first < value;
            });
        for (; found != ::std::end(COMPOSITIONS) && found->first == first; ++found) {
            if (found->second == second) {
                return found->composed;
            }
        }
        return 0;
    }

    void decomposeInto(u32 codepoint, ::std::vector<u32>& out) {
        if (HANGUL_S_BASE <= codepoint && codepoint < HANGUL_S_BASE + HANGUL_S_COUNT) {
            const u32 index = codepoint - HANGUL_S_BASE;
            out.push_back(HANGUL_L_BASE + index / HANGUL_N_COUNT);
            out.push_back(HANGUL_V_BASE + (index % HANGUL_N_COUNT) / HANGUL_T_COUNT);
            if (index % HANGUL_T_COUNT != 0) {
                out.push_back(HANGUL_T_BASE + index % HANGUL_T_COUNT);
            }
            return;
        }
        if (const auto* entry = decompositionOf(codepoint)) {
            decomposeInto(entry->first, out);
            if (entry->second != 0) {
                decomposeInto(entry->second, out);
            }
            return;
        }
        out.push_back(codepoint);
    }

    /// Sort each run of combining marks by class, keeping equal classes in order.
    void orderCanonically(::std::vector<u32>& text) {
        for (size_t i = 1; i < text.size(); i++) {
            const auto cls = combiningClass(text[i]);
            if (cls == 0) {
                continue;
            }
            size_t j = i;
            while (j > 0) {
                const auto previous = combiningClass(text[j - 1]);
                if (previous == 0 || previous <= cls) {
                    break;
                }
                ::std::swap(text[j - 1], text[j]);
                j--;
            }
        }
    }

    void compose(::std::vector<u32>& text) {
        if (text.empty()) {
            return;
        }
        size_t starter = 0;
        u8 lastClass = combiningClass(text[0]) == 0 ? 0 : 255;
        size_t out = 1;
        for (size_t i = 1; i < text.size(); i++) {
            const auto cls = combiningClass(text[i]);
            // A mark may only compose with the starter when nothing between them
            // blocks it: no earlier mark of the same or a higher class.
            if (lastClass < cls || (lastClass == 0 && cls == 0)) {
                if (const auto composed = composedPair(text[starter], text[i])) {
                    text[starter] = composed;
                    continue;
                }
            }
            if (cls == 0) {
                starter = out;
            }
            lastClass = cls;
            text[out++] = text[i];
        }
        text.resize(out);
    }

    void appendUtf8(::std::string& out, u32 codepoint) {
        if (codepoint < 0x80) {
            out += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    }

    /// Decode UTF-8. Malformed input is not this file's business to diagnose: a
    /// bad byte becomes its own codepoint so the text survives unchanged.
    ::std::vector<u32> decodeUtf8(const ::std::string& text) {
        ::std::vector<u32> out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size();) {
            const auto lead = static_cast<u8>(text[i]);
            size_t length = 1;
            u32 codepoint = lead;
            if ((lead & 0xE0) == 0xC0) {
                length = 2;
                codepoint = lead & 0x1F;
            } else if ((lead & 0xF0) == 0xE0) {
                length = 3;
                codepoint = lead & 0x0F;
            } else if ((lead & 0xF8) == 0xF0) {
                length = 4;
                codepoint = lead & 0x07;
            }
            if (i + length > text.size()) {
                out.push_back(lead);
                i++;
                continue;
            }
            bool valid = true;
            for (size_t j = 1; j < length; j++) {
                const auto next = static_cast<u8>(text[i + j]);
                if ((next & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
                codepoint = (codepoint << 6) | (next & 0x3F);
            }
            if (!valid) {
                out.push_back(lead);
                i++;
                continue;
            }
            out.push_back(codepoint);
            i += length;
        }
        return out;
    }

    bool isAscii(const ::std::string& text) {
        for (const char c : text) {
            if (static_cast<u8>(c) >= 0x80) {
                return false;
            }
        }
        return true;
    }
}

::std::string unicodeNormaliseNfc(const ::std::string& text) {
    if (isAscii(text)) {
        return text;
    }

    auto codepoints = decodeUtf8(text);
    ::std::vector<u32> decomposed;
    decomposed.reserve(codepoints.size() * 2);
    for (const auto codepoint : codepoints) {
        decomposeInto(codepoint, decomposed);
    }
    orderCanonically(decomposed);
    compose(decomposed);

    ::std::string out;
    out.reserve(text.size());
    for (const auto codepoint : decomposed) {
        appendUtf8(out, codepoint);
    }
    return out;
}

bool unicodeIsNfc(const ::std::string& text) {
    return isAscii(text) || unicodeNormaliseNfc(text) == text;
}
