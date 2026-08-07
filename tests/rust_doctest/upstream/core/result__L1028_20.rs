// Extracted from library/core/src/result.rs:1028
#![allow(unused)]
fn main() {
    let x: Result<u32, &str> = Ok(7);
    assert_eq!(x.iter().next(), Some(&7));

    let x: Result<u32, &str> = Err("nothing!");
    assert_eq!(x.iter().next(), None);
}
