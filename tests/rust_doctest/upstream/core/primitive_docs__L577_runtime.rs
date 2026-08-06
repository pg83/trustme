// Extracted from library/core/src/primitive_docs.rs:577
#![allow(unused)]
fn main() {
    #[derive(Debug, Default, Copy, Clone)]
    #[repr(C, packed)]
    struct S {
        aligned: u8,
        unaligned: u32,
    }
    let s = S::default();
    let p = &raw const s.unaligned; // not allowed with coercion
}
