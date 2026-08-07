// Extracted from library/std/src/sync/once_lock.rs:333
#![allow(unused)]
#![feature(once_cell_get_mut)]
fn main() {

    use std::sync::OnceLock;

    let mut cell = OnceLock::new();
    let value = cell.get_mut_or_init(|| 92);
    assert_eq!(*value, 92);

    *value += 2;
    assert_eq!(*value, 94);

    let value = cell.get_mut_or_init(|| unreachable!());
    assert_eq!(*value, 94);
}
