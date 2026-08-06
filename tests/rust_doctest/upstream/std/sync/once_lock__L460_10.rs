// Extracted from library/std/src/sync/once_lock.rs:460
#![allow(unused)]
fn main() {
    use std::sync::OnceLock;
    
    let cell: OnceLock<String> = OnceLock::new();
    assert_eq!(cell.into_inner(), None);
    
    let cell = OnceLock::new();
    cell.set("hello".to_string()).unwrap();
    assert_eq!(cell.into_inner(), Some("hello".to_string()));
}
