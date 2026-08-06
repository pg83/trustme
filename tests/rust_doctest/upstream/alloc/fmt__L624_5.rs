// Extracted from library/alloc/src/fmt.rs:624
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::fmt;
    
    let s = fmt::format(format_args!("Hello, {}!", "world"));
    assert_eq!(s, "Hello, world!");
}
