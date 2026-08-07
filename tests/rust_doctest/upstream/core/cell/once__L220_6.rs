// Extracted from library/core/src/cell/once.rs:220
#![allow(unused)]
#![feature(once_cell_try)]
fn main() {

    use std::cell::OnceCell;

    let cell = OnceCell::new();
    assert_eq!(cell.get_or_try_init(|| Err(())), Err(()));
    assert!(cell.get().is_none());
    let value = cell.get_or_try_init(|| -> Result<i32, ()> {
        Ok(92)
    });
    assert_eq!(value, Ok(&92));
    assert_eq!(cell.get(), Some(&92))
}
