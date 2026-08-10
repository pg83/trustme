// Extracted from library/core/src/slice/mod.rs:3903
#![allow(unused)]
fn main() {
    let mut bytes = *b"Hello, World!";

    bytes.copy_within(1..5, 8);

    assert_eq!(&bytes, b"Hello, Wello!");
}
