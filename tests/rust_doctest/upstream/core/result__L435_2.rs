// Extracted from library/core/src/result.rs:435
#![allow(unused)]
fn main() {
    assert!(Ok(1) < Err(0));
    let x: Result<i32, ()> = Ok(0);
    let y = Ok(1);
    assert!(x < y);
    let x: Result<(), i32> = Err(0);
    let y = Err(1);
    assert!(x < y);
}
