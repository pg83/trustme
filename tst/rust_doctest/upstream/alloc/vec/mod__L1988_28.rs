// Extracted from library/alloc/src/vec/mod.rs:1988
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = vec!["foo", "bar", "baz", "qux"];

    assert_eq!(v.swap_remove(1), "bar");
    assert_eq!(v, ["foo", "qux", "baz"]);

    assert_eq!(v.swap_remove(0), "foo");
    assert_eq!(v, ["baz", "qux"]);
}
