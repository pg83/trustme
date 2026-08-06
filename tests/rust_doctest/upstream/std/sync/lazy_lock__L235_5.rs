// Extracted from library/std/src/sync/lazy_lock.rs:235
#![allow(unused)]
#![feature(lazy_get)]
fn main() {
    
    use std::sync::LazyLock;
    
    let mut lazy = LazyLock::new(|| 92);
    
    assert_eq!(LazyLock::get_mut(&mut lazy), None);
    let _ = LazyLock::force(&lazy);
    *LazyLock::get_mut(&mut lazy).unwrap() = 44;
    assert_eq!(*lazy, 44);
}
