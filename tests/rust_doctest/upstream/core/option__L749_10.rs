// Extracted from library/core/src/option.rs:749
#![allow(unused)]
fn main() {
    let mut x = Some(2);
    match x.as_mut() {
        Some(v) => *v = 42,
        None => {},
    }
    assert_eq!(x, Some(42));
}
