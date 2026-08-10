// Extracted from library/core/src/primitive_docs.rs:417
#![allow(unused)]
fn main() {
    let mut chars = "é".chars();
    // U+00e9: 'latin small letter e with acute'
    assert_eq!(Some('\u{00e9}'), chars.next());
    assert_eq!(None, chars.next());

    let mut chars = "é".chars();
    // U+0065: 'latin small letter e'
    assert_eq!(Some('\u{0065}'), chars.next());
    // U+0301: 'combining acute accent'
    assert_eq!(Some('\u{0301}'), chars.next());
    assert_eq!(None, chars.next());
}
