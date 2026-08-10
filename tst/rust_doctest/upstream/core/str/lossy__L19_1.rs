// Extracted from library/core/src/str/lossy.rs:19
use std::fmt::Write as _;

pub fn cstr_literal(bytes: &[u8]) -> String {
    let mut repr = String::new();
    repr.push_str("c\"");
    for chunk in bytes.utf8_chunks() {
        for ch in chunk.valid().chars() {
            // Escapes \0, \t, \r, \n, \\, \', \", and uses \u{...} for non-printable characters.
            write!(repr, "{}", ch.escape_debug()).unwrap();
        }
        for byte in chunk.invalid() {
            write!(repr, "\\x{:02X}", byte).unwrap();
        }
    }
    repr.push('"');
    repr
}

fn main() {
    let lit = cstr_literal(b"\xferris the \xf0\x9f\xa6\x80\x07");
    let expected = stringify!(c"\xFErris the 🦀\u{7}");
    assert_eq!(lit, expected);
}
