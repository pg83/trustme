// Extracted from library/core/src/slice/mod.rs:2626
#![allow(unused)]
fn main() {
    let v = [10, 40, 30];
    assert!(v.ends_with(&[30]));
    assert!(v.ends_with(&[40, 30]));
    assert!(v.ends_with(&v));
    assert!(!v.ends_with(&[50]));
    assert!(!v.ends_with(&[50, 30]));
}
