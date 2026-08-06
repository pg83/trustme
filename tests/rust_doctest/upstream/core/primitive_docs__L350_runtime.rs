// Extracted from library/core/src/primitive_docs.rs:350
#![allow(unused)]
fn main() {
    // Panics; from_u32 returns None.
    char::from_u32(0xDE01).unwrap();
}
