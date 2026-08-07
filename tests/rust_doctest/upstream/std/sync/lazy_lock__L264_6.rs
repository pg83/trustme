// Extracted from library/std/src/sync/lazy_lock.rs:264
#![allow(unused)]
#![feature(lazy_get)]
fn main() {

    use std::sync::LazyLock;

    let lazy = LazyLock::new(|| 92);

    assert_eq!(LazyLock::get(&lazy), None);
    let _ = LazyLock::force(&lazy);
    assert_eq!(LazyLock::get(&lazy), Some(&92));
}
