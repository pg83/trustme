// Extracted from library/core/src/str/mod.rs:1201
#![allow(unused)]
fn main() {
    let mut iter = "A few words".split_ascii_whitespace();

    assert_eq!(Some("A"), iter.next());
    assert_eq!(Some("few"), iter.next());
    assert_eq!(Some("words"), iter.next());

    assert_eq!(None, iter.next());
}
