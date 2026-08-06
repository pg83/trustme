// Extracted from library/core/src/slice/mod.rs:123
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    assert!(!a.is_empty());
    
    let b: &[i32] = &[];
    assert!(b.is_empty());
}
