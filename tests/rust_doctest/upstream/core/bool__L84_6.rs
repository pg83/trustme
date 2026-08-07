// Extracted from library/core/src/bool.rs:84
#![allow(unused)]
#![feature(bool_to_result)]
fn main() {

    let mut a = 0;
    let mut function_with_side_effects = || { a += 1; };

    assert!(true.ok_or(function_with_side_effects()).is_ok());
    assert!(false.ok_or(function_with_side_effects()).is_err());

    // `a` is incremented twice because the value passed to `ok_or` is
    // evaluated eagerly.
    assert_eq!(a, 2);
}
