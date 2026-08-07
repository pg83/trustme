// Extracted from library/std/src/sync/lazy_lock.rs:199
#![allow(unused)]
fn main() {
    use std::sync::LazyLock;

    let lazy = LazyLock::new(|| 92);

    assert_eq!(LazyLock::force(&lazy), &92);
    assert_eq!(&*lazy, &92);
}
