// Extracted from library/alloc/src/vec/mod.rs:3154
#![allow(unused)]
#![feature(vec_into_chunks)]
extern crate alloc;
fn main() {
    
    let vec = vec![0, 1, 2, 3, 4, 5, 6, 7];
    assert_eq!(vec.into_chunks::<3>(), [[0, 1, 2], [3, 4, 5]]);
    
    let vec = vec![0, 1, 2, 3];
    let chunks: Vec<[u8; 10]> = vec.into_chunks();
    assert!(chunks.is_empty());
    
    let flat = vec![0; 8 * 8 * 8];
    let reshaped: Vec<[[[u8; 8]; 8]; 8]> = flat.into_chunks().into_chunks().into_chunks();
    assert_eq!(reshaped.len(), 1);
}
