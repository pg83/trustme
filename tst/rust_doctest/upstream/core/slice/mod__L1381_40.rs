// Extracted from library/core/src/slice/mod.rs:1381
#![allow(unused)]
fn main() {
    let slice = ['R', 'u', 's', 't'];
    let (chunks, []) = slice.as_chunks::<2>() else {
        panic!("slice didn't have even length")
    };
    assert_eq!(chunks, &[['R', 'u'], ['s', 't']]);
}
