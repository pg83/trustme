// Extracted from library/core/src/str/mod.rs:2436
#![allow(unused)]
fn main() {
    assert_eq!("bar:foo".strip_suffix(":foo"), Some("bar"));
    assert_eq!("bar:foo".strip_suffix("bar"), None);
    assert_eq!("foofoo".strip_suffix("foo"), Some("foo"));
}
