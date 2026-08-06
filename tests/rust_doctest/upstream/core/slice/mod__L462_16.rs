// Extracted from library/core/src/slice/mod.rs:462
#![allow(unused)]
fn main() {
    let x = &mut [0, 1, 2];
    
    if let Some((elements, last)) = x.split_last_chunk_mut::<2>() {
        last[0] = 3;
        last[1] = 4;
        elements[0] = 5;
    }
    assert_eq!(x, &[5, 3, 4]);
    
    assert_eq!(None, x.split_last_chunk_mut::<4>());
}
