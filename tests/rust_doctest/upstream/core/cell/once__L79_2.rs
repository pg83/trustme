// Extracted from library/core/src/cell/once.rs:79
#![allow(unused)]
fn main() {
    use std::cell::OnceCell;
    
    let cell = OnceCell::new();
    assert!(cell.get().is_none());
    
    assert_eq!(cell.set(92), Ok(()));
    assert_eq!(cell.set(62), Err(62));
    
    assert!(cell.get().is_some());
}
