// Extracted from library/core/src/bool.rs:108
#![allow(unused)]
#![feature(bool_to_result)]
fn main() {

    assert_eq!(false.ok_or_else(|| 0), Err(0));
    assert_eq!(true.ok_or_else(|| 0), Ok(()));
}
