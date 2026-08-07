// Extracted from library/core/src/cell/once.rs:150
#![allow(unused)]
fn main() {
    use std::cell::OnceCell;

    let cell = OnceCell::new();
    let value = cell.get_or_init(|| 92);
    assert_eq!(value, &92);
    let value = cell.get_or_init(|| unreachable!());
    assert_eq!(value, &92);
}
