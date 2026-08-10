// Extracted from library/core/src/str/mod.rs:1038
#![allow(unused)]
fn main() {
    let y = "y̆";

    let mut chars = y.chars();

    assert_eq!(Some('y'), chars.next()); // not 'y̆'
    assert_eq!(Some('\u{0306}'), chars.next());

    assert_eq!(None, chars.next());
}
