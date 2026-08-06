// Extracted from library/core/src/cell/once.rs:256
#![allow(unused)]
#![feature(once_cell_get_mut)]
fn main() {
    
    use std::cell::OnceCell;
    
    let mut cell: OnceCell<u32> = OnceCell::new();
    
    // Failed attempts to initialize the cell do not change its contents
    assert!(cell.get_mut_or_try_init(|| "not a number!".parse()).is_err());
    assert!(cell.get().is_none());
    
    let value = cell.get_mut_or_try_init(|| "1234".parse());
    assert_eq!(value, Ok(&mut 1234));
    
    let Ok(value) = value else { return; };
    *value += 2;
    assert_eq!(cell.get(), Some(&1236))
}
