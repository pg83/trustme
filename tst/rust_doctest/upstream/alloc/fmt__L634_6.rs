// Extracted from library/alloc/src/fmt.rs:634
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = format!("Hello, {}!", "world");
    assert_eq!(s, "Hello, world!");
}
