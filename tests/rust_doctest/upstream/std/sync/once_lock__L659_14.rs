// Extracted from library/std/src/sync/once_lock.rs:659
#![allow(unused)]
fn main() {
    use std::sync::OnceLock;
    
    let five = OnceLock::new();
    five.set(5).unwrap();
    
    let also_five = OnceLock::new();
    also_five.set(5).unwrap();
    
    assert!(five == also_five);
    
    assert!(OnceLock::<u32>::new() == OnceLock::<u32>::new());
}
