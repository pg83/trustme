// Extracted from library/core/src/bool.rs:115
#![allow(unused)]
#![feature(bool_to_result)]
fn main() {

    let mut a = 0;

    assert!(true.ok_or_else(|| { a += 1; }).is_ok());
    assert!(false.ok_or_else(|| { a += 1; }).is_err());

    // `a` is incremented once because the closure is evaluated lazily by
    // `ok_or_else`.
    assert_eq!(a, 1);
}
