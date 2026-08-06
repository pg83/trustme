// Extracted from library/core/src/cell/once.rs:109
#![allow(unused)]
#![feature(once_cell_try_insert)]
fn main() {
    
    use std::cell::OnceCell;
    
    let cell = OnceCell::new();
    assert!(cell.get().is_none());
    
    assert_eq!(cell.try_insert(92), Ok(&92));
    assert_eq!(cell.try_insert(62), Err((&92, 62)));
    
    assert!(cell.get().is_some());
}
