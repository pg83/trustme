// Extracted from library/core/src/str/mod.rs:2408
#![allow(unused)]
fn main() {
    assert_eq!("foo:bar".strip_prefix("foo:"), Some("bar"));
    assert_eq!("foo:bar".strip_prefix("bar"), None);
    assert_eq!("foofoo".strip_prefix("foo"), Some("foo"));
}
