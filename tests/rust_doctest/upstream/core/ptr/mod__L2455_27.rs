// Extracted from library/core/src/ptr/mod.rs:2455
#![allow(unused)]
fn main() {
    use std::ptr;

    let whole: &[i32; 3] = &[1, 2, 3];
    let first: &i32 = &whole[0];

    assert!(ptr::addr_eq(whole, first));
    assert!(!ptr::eq::<dyn std::fmt::Debug>(whole, first));
}
