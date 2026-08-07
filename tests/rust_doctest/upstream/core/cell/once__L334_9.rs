// Extracted from library/core/src/cell/once.rs:334
#![allow(unused)]
fn main() {
    use std::cell::OnceCell;

    let mut cell: OnceCell<String> = OnceCell::new();
    assert_eq!(cell.take(), None);

    let mut cell = OnceCell::new();
    let _ = cell.set("hello".to_owned());
    assert_eq!(cell.take(), Some("hello".to_owned()));
    assert_eq!(cell.get(), None);
}
