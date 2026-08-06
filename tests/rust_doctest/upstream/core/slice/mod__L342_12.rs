// Extracted from library/core/src/slice/mod.rs:342
#![allow(unused)]
fn main() {
    let x = &mut [0, 1, 2];
    
    if let Some(first) = x.first_chunk_mut::<2>() {
        first[0] = 5;
        first[1] = 4;
    }
    assert_eq!(x, &[5, 4, 2]);
    
    assert_eq!(None, x.first_chunk_mut::<4>());
}
