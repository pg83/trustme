// Extracted from library/core/src/option.rs:1207
#![allow(unused)]
fn main() {
    let x = Some("foo");
    assert_eq!(x.map_or(42, |v| v.len()), 3);

    let x: Option<&str> = None;
    assert_eq!(x.map_or(42, |v| v.len()), 42);
}
