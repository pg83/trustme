// Extracted from library/core/src/convert/mod.rs:538
#![allow(unused)]
fn main() {
    let string = "hello".to_string();
    let other_string = String::from("hello");

    assert_eq!(string, other_string);
}
