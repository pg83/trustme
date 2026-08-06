// Extracted from library/core/src/macros/mod.rs:1135
#![allow(unused)]
fn main() {
    let s = concat!("test", 10, 'b', true);
    assert_eq!(s, "test10btrue");
}
