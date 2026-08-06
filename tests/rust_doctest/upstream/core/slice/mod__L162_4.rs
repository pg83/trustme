// Extracted from library/core/src/slice/mod.rs:162
#![allow(unused)]
fn main() {
    let x = &mut [0, 1, 2];
    
    if let Some(first) = x.first_mut() {
        *first = 5;
    }
    assert_eq!(x, &[5, 1, 2]);
    
    let y: &mut [i32] = &mut [];
    assert_eq!(None, y.first_mut());
}
