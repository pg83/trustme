// Extracted from library/core/src/slice/mod.rs:2595
#![allow(unused)]
fn main() {
    let v = [10, 40, 30];
    assert!(v.starts_with(&[10]));
    assert!(v.starts_with(&[10, 40]));
    assert!(v.starts_with(&v));
    assert!(!v.starts_with(&[50]));
    assert!(!v.starts_with(&[10, 50]));
}
