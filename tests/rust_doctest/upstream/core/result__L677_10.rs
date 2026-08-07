// Extracted from library/core/src/result.rs:677
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(2);
    assert_eq!(x.ok(), Some(2));

    let x: Result<u32, &str> = Err("Nothing here");
    assert_eq!(x.ok(), None);
}
