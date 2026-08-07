// Extracted from library/core/src/str/mod.rs:1015
#![allow(unused)]
fn main() {
    let word = "goodbye";

    let count = word.chars().count();
    assert_eq!(7, count);

    let mut chars = word.chars();

    assert_eq!(Some('g'), chars.next());
    assert_eq!(Some('o'), chars.next());
    assert_eq!(Some('o'), chars.next());
    assert_eq!(Some('d'), chars.next());
    assert_eq!(Some('b'), chars.next());
    assert_eq!(Some('y'), chars.next());
    assert_eq!(Some('e'), chars.next());

    assert_eq!(None, chars.next());
}
