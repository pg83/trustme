// Extracted from library/alloc/src/vec/mod.rs:4211
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(Vec::from("123"), vec![b'1', b'2', b'3']);
}
