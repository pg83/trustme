// Extracted from library/core/src/cell/once.rs:23
#![allow(unused)]
fn main() {
    use std::cell::OnceCell;

    let cell = OnceCell::new();
    assert!(cell.get().is_none());

    let value: &String = cell.get_or_init(|| {
        "Hello, World!".to_string()
    });
    assert_eq!(value, "Hello, World!");
    assert!(cell.get().is_some());
}
