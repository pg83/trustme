// Extracted from library/core/src/bool.rs:77
#![allow(unused)]
#![feature(bool_to_result)]
fn main() {
    
    assert_eq!(false.ok_or(0), Err(0));
    assert_eq!(true.ok_or(0), Ok(()));
}
