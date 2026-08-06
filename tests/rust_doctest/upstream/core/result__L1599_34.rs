// Extracted from library/core/src/result.rs:1599
#![allow(unused)]
fn main() {
    let val = 12;
    let x: Result<&i32, i32> = Ok(&val);
    assert_eq!(x, Ok(&12));
    let copied = x.copied();
    assert_eq!(copied, Ok(12));
}
