// Extracted from library/core/src/cell/lazy.rs:70
#![allow(unused)]
#![feature(lazy_cell_into_inner)]
fn main() {
    
    use std::cell::LazyCell;
    
    let hello = "Hello, World!".to_string();
    
    let lazy = LazyCell::new(|| hello.to_uppercase());
    
    assert_eq!(&*lazy, "HELLO, WORLD!");
    assert_eq!(LazyCell::into_inner(lazy).ok(), Some("HELLO, WORLD!".to_string()));
}
