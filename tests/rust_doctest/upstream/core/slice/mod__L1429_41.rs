// Extracted from library/core/src/slice/mod.rs:1429
#![allow(unused)]
fn main() {
    let slice = ['l', 'o', 'r', 'e', 'm'];
    let (remainder, chunks) = slice.as_rchunks();
    assert_eq!(remainder, &['l']);
    assert_eq!(chunks, &[['o', 'r'], ['e', 'm']]);
}
