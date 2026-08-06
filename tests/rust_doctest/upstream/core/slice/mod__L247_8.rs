// Extracted from library/core/src/slice/mod.rs:247
#![allow(unused)]
fn main() {
    let x = &mut [0, 1, 2];
    
    if let Some((last, elements)) = x.split_last_mut() {
        *last = 3;
        elements[0] = 4;
        elements[1] = 5;
    }
    assert_eq!(x, &[4, 5, 3]);
}
