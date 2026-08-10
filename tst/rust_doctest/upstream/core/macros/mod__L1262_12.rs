// Extracted from library/core/src/macros/mod.rs:1262
#![allow(unused)]
fn main() {
    let one_plus_one = stringify!(1 + 1);
    assert_eq!(one_plus_one, "1 + 1");
}
