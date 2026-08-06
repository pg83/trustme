// Extracted from library/std/src/sync/once_lock.rs:425
#![allow(unused)]
#![feature(once_cell_get_mut)]
fn main() {
    
    use std::sync::OnceLock;
    
    let mut cell: OnceLock<u32> = OnceLock::new();
    
    // Failed attempts to initialize the cell do not change its contents
    assert!(cell.get_mut_or_try_init(|| "not a number!".parse()).is_err());
    assert!(cell.get().is_none());
    
    let value = cell.get_mut_or_try_init(|| "1234".parse());
    assert_eq!(value, Ok(&mut 1234));
    *value.unwrap() += 2;
    assert_eq!(cell.get(), Some(&1236))
}
