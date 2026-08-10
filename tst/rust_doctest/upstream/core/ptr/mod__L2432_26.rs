// Extracted from library/core/src/ptr/mod.rs:2432
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    assert!(std::ptr::eq(&a[..3], &a[..3]));
    assert!(!std::ptr::eq(&a[..2], &a[..3]));
    assert!(!std::ptr::eq(&a[0..2], &a[1..3]));
}
