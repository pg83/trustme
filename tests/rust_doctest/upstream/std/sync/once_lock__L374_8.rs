// Extracted from library/std/src/sync/once_lock.rs:374
#![allow(unused)]
#![feature(once_cell_try)]
fn main() {
    
    use std::sync::OnceLock;
    
    let cell = OnceLock::new();
    assert_eq!(cell.get_or_try_init(|| Err(())), Err(()));
    assert!(cell.get().is_none());
    let value = cell.get_or_try_init(|| -> Result<i32, ()> {
        Ok(92)
    });
    assert_eq!(value, Ok(&92));
    assert_eq!(cell.get(), Some(&92))
}
