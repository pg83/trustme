// Extracted from library/std/src/sync/lazy_lock.rs:77
#![allow(unused)]
fn main() {
    use std::sync::LazyLock;

    let hello = "Hello, World!".to_string();

    let lazy = LazyLock::new(|| hello.to_uppercase());

    assert_eq!(&*lazy, "HELLO, WORLD!");
}
