// Extracted from library/core/src/result.rs:998
#![allow(unused)]
fn main() {
    let mut s = "HELLO".to_string();
    let mut x: Result<String, u32> = Ok("hello".to_string());
    let y: Result<&mut str, &mut u32> = Ok(&mut s);
    assert_eq!(x.as_deref_mut().map(|x| { x.make_ascii_uppercase(); x }), y);

    let mut i = 42;
    let mut x: Result<String, u32> = Err(42);
    let y: Result<&mut str, &mut u32> = Err(&mut i);
    assert_eq!(x.as_deref_mut().map(|x| { x.make_ascii_uppercase(); x }), y);
}
