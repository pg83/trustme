// Extracted from library/std/src/sync/once_lock.rs:485
#![allow(unused)]
fn main() {
    use std::sync::OnceLock;
    
    let mut cell: OnceLock<String> = OnceLock::new();
    assert_eq!(cell.take(), None);
    
    let mut cell = OnceLock::new();
    cell.set("hello".to_string()).unwrap();
    assert_eq!(cell.take(), Some("hello".to_string()));
    assert_eq!(cell.get(), None);
}
