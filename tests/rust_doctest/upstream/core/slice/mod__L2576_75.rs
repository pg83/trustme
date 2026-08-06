// Extracted from library/core/src/slice/mod.rs:2576
#![allow(unused)]
fn main() {
    let v = [String::from("hello"), String::from("world")]; // slice of `String`
    assert!(v.iter().any(|e| e == "hello")); // search with `&str`
    assert!(!v.iter().any(|e| e == "hi"));
}
