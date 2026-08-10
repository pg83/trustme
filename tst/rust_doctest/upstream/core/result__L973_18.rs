// Extracted from library/core/src/result.rs:973
#![allow(unused)]
fn main() {
    let x: Result<String, u32> = Ok("hello".to_string());
    let y: Result<&str, &u32> = Ok("hello");
    assert_eq!(x.as_deref(), y);

    let x: Result<String, u32> = Err(42);
    let y: Result<&str, &u32> = Err(&42);
    assert_eq!(x.as_deref(), y);
}
