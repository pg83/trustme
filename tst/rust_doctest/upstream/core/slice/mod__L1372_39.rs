// Extracted from library/core/src/slice/mod.rs:1372
#![allow(unused)]
fn main() {
    let slice = ['l', 'o', 'r', 'e', 'm'];
    let (chunks, remainder) = slice.as_chunks();
    assert_eq!(chunks, &[['l', 'o'], ['r', 'e']]);
    assert_eq!(remainder, &['m']);
}
