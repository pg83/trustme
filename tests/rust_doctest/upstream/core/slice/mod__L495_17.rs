// Extracted from library/core/src/slice/mod.rs:495
#![allow(unused)]
fn main() {
    let u = [10, 40, 30];
    assert_eq!(Some(&[40, 30]), u.last_chunk::<2>());
    
    let v: &[i32] = &[10];
    assert_eq!(None, v.last_chunk::<2>());
    
    let w: &[i32] = &[];
    assert_eq!(Some(&[]), w.last_chunk::<0>());
}
