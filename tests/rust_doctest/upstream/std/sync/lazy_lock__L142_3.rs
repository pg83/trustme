// Extracted from library/std/src/sync/lazy_lock.rs:142
#![allow(unused)]
#![feature(lazy_get)]
fn main() {
    use std::sync::LazyLock;

    let mut lazy = LazyLock::new(|| 92);

    let p = LazyLock::force_mut(&mut lazy);
    assert_eq!(*p, 92);
    *p = 44;
    assert_eq!(*lazy, 44);
}
