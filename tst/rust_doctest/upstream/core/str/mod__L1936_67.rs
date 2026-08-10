// Extracted from library/core/src/str/mod.rs:1936
#![allow(unused)]
fn main() {
    assert_eq!("cfg".split_once('='), None);
    assert_eq!("cfg=".split_once('='), Some(("cfg", "")));
    assert_eq!("cfg=foo".split_once('='), Some(("cfg", "foo")));
    assert_eq!("cfg=foo=bar".split_once('='), Some(("cfg", "foo=bar")));
}
