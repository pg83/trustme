// Extracted from library/core/src/slice/mod.rs:778
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    let x = &a[1] as *const _;
    let y = &5 as *const _;
    
    assert!(a.as_ptr_range().contains(&x));
    assert!(!a.as_ptr_range().contains(&y));
}
