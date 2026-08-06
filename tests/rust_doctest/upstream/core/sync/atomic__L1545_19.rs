// Extracted from library/core/src/sync/atomic.rs:1545
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicPtr, Ordering};
    
    let mut data = 10;
    let mut atomic_ptr = AtomicPtr::new(&mut data);
    let mut other_data = 5;
    *atomic_ptr.get_mut() = &mut other_data;
    assert_eq!(unsafe { *atomic_ptr.load(Ordering::SeqCst) }, 5);
}
