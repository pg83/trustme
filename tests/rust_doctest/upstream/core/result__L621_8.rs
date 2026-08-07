// Extracted from library/core/src/result.rs:621
#![allow(unused)]
fn main() {
    let x: Result<i32, &str> = Ok(-3);
    assert_eq!(x.is_err(), false);

    let x: Result<i32, &str> = Err("Some error message");
    assert_eq!(x.is_err(), true);
}
