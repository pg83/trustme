// Extracted from library/core/src/mem/mod.rs:762
#![allow(unused)]
fn main() {
    use std::mem;
    
    let mut v: Vec<i32> = vec![1, 2];
    
    let old_v = mem::take(&mut v);
    assert_eq!(vec![1, 2], old_v);
    assert!(v.is_empty());
}
