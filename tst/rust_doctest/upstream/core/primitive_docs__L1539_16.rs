// Extracted from library/core/src/primitive_docs.rs:1539
#![allow(unused)]
fn main() {
    use std::ptr;

    let five = 5;
    let other_five = 5;
    let five_ref = &five;
    let same_five_ref = &five;
    let other_five_ref = &other_five;

    assert!(five_ref == same_five_ref);
    assert!(five_ref == other_five_ref);

    assert!(ptr::eq(five_ref, same_five_ref));
    assert!(!ptr::eq(five_ref, other_five_ref));
}
