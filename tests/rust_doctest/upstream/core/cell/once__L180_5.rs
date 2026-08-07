// Extracted from library/core/src/cell/once.rs:180
#![allow(unused)]
#![feature(once_cell_get_mut)]
fn main() {

    use std::cell::OnceCell;

    let mut cell = OnceCell::new();
    let value = cell.get_mut_or_init(|| 92);
    assert_eq!(*value, 92);

    *value += 2;
    assert_eq!(*value, 94);

    let value = cell.get_mut_or_init(|| unreachable!());
    assert_eq!(*value, 94);
}
