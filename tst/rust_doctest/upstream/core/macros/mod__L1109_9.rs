// Extracted from library/core/src/macros/mod.rs:1109
#![feature(concat_bytes)]

fn main() {
let s: &[u8; 6] = concat_bytes!(b'A', b"BC", [68, b'E', 70]);
assert_eq!(s, b"ABCDEF");
}
