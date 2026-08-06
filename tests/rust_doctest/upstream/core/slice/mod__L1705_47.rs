// Extracted from library/core/src/slice/mod.rs:1705
#![allow(unused)]
fn main() {
    let v = &mut [0, 0, 0, 0, 0];
    let mut count = 1;
    
    for chunk in v.rchunks_mut(2) {
        for elem in chunk.iter_mut() {
            *elem += count;
        }
        count += 1;
    }
    assert_eq!(v, &[3, 2, 2, 1, 1]);
}
