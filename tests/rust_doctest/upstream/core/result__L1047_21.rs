// Extracted from library/core/src/result.rs:1047
#![allow(unused)]
fn main() {
    let mut x: Result<u32, &str> = Ok(7);
    match x.iter_mut().next() {
        Some(v) => *v = 40,
        None => {},
    }
    assert_eq!(x, Ok(40));

    let mut x: Result<u32, &str> = Err("nothing!");
    assert_eq!(x.iter_mut().next(), None);
}
