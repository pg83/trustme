#pragma once

#include <string>

/// Canonical composition (NFC) of UTF-8 text. Rust normalises identifiers, so
/// `Résumé` written with a precomposed `é` and with `e` plus a combining acute
/// are the same name.
extern ::std::string unicodeNormaliseNfc(const ::std::string& text);

/// Whether the text is already normalised. Text that is pure ASCII always is,
/// which is the common case and costs one pass.
extern bool unicodeIsNfc(const ::std::string& text);
