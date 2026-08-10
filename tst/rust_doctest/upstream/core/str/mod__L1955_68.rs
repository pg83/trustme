// Extracted from library/core/src/str/mod.rs:1955
#![allow(unused)]
fn main() {
    assert_eq!("cfg".rsplit_once('='), None);
    assert_eq!("cfg=foo".rsplit_once('='), Some(("cfg", "foo")));
    assert_eq!("cfg=foo=bar".rsplit_once('='), Some(("cfg=foo", "bar")));
}
