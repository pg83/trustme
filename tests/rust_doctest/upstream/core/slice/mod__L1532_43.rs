// Extracted from library/core/src/slice/mod.rs:1532
#![allow(unused)]
fn main() {
    let v = &mut [0, 0, 0, 0, 0];
    let mut count = 1;
    
    let (chunks, remainder) = v.as_chunks_mut();
    remainder[0] = 9;
    for chunk in chunks {
        *chunk = [count; 2];
        count += 1;
    }
    assert_eq!(v, &[1, 1, 2, 2, 9]);
}
