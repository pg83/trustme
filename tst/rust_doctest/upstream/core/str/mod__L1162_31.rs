// Extracted from library/core/src/str/mod.rs:1162
#![allow(unused)]
fn main() {
    let mut iter = " Mary   had\ta\u{2009}little  \n\t lamb".split_whitespace();
    assert_eq!(Some("Mary"), iter.next());
    assert_eq!(Some("had"), iter.next());
    assert_eq!(Some("a"), iter.next());
    assert_eq!(Some("little"), iter.next());
    assert_eq!(Some("lamb"), iter.next());

    assert_eq!(None, iter.next());
}
