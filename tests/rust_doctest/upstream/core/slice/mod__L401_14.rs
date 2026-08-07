// Extracted from library/core/src/slice/mod.rs:401
#![allow(unused)]
fn main() {
    let x = &mut [0, 1, 2];

    if let Some((first, elements)) = x.split_first_chunk_mut::<2>() {
        first[0] = 3;
        first[1] = 4;
        elements[0] = 5;
    }
    assert_eq!(x, &[3, 4, 5]);

    assert_eq!(None, x.split_first_chunk_mut::<4>());
}
