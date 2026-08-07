// Extracted from library/alloc/src/vec/mod.rs:2361
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec!["foo", "bar", "Bar", "baz", "bar"];

    vec.dedup_by(|a, b| a.eq_ignore_ascii_case(b));

    assert_eq!(vec, ["foo", "bar", "baz", "bar"]);
}
