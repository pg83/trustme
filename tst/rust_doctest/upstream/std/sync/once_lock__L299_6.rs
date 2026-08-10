// Extracted from library/std/src/sync/once_lock.rs:299
#![allow(unused)]
fn main() {
    use std::sync::OnceLock;

    let cell = OnceLock::new();
    let value = cell.get_or_init(|| 92);
    assert_eq!(value, &92);
    let value = cell.get_or_init(|| unreachable!());
    assert_eq!(value, &92);
}
