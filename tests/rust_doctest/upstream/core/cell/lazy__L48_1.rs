// Extracted from library/core/src/cell/lazy.rs:48
#![allow(unused)]
fn main() {
    use std::cell::LazyCell;
    
    let hello = "Hello, World!".to_string();
    
    let lazy = LazyCell::new(|| hello.to_uppercase());
    
    assert_eq!(&*lazy, "HELLO, WORLD!");
}
