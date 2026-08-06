// Extracted from library/std/src/sync/poison/once.rs:340
#![allow(unused)]
fn main() {
    use std::sync::Once;
    use std::thread;
    
    static INIT: Once = Once::new();
    
    // poison the once
    let handle = thread::spawn(|| {
        INIT.call_once(|| panic!());
    });
    assert!(handle.join().is_err());
    
    INIT.call_once_force(|state| {
        assert!(state.is_poisoned());
    });
}
