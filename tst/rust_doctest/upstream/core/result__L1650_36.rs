// Extracted from library/core/src/result.rs:1650
#![allow(unused)]
fn main() {
    let mut val = 12;
    let x: Result<&mut i32, i32> = Ok(&mut val);
    assert_eq!(x, Ok(&mut 12));
    let copied = x.copied();
    assert_eq!(copied, Ok(12));
}
