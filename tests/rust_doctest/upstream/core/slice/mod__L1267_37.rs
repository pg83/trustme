// Extracted from library/core/src/slice/mod.rs:1267
#![allow(unused)]
fn main() {
    let v = &mut [0, 0, 0, 0, 0];
    let mut count = 1;
    
    for chunk in v.chunks_exact_mut(2) {
        for elem in chunk.iter_mut() {
            *elem += count;
        }
        count += 1;
    }
    assert_eq!(v, &[1, 1, 2, 2, 0]);
}
