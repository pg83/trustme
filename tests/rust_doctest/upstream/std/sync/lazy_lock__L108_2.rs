// Extracted from library/std/src/sync/lazy_lock.rs:108
#![allow(unused)]
#![feature(lazy_cell_into_inner)]
fn main() {
    
    use std::sync::LazyLock;
    
    let hello = "Hello, World!".to_string();
    
    let lazy = LazyLock::new(|| hello.to_uppercase());
    
    assert_eq!(&*lazy, "HELLO, WORLD!");
    assert_eq!(LazyLock::into_inner(lazy).ok(), Some("HELLO, WORLD!".to_string()));
}
