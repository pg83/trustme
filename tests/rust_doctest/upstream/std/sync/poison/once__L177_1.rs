// Extracted from library/std/src/sync/poison/once.rs:177
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
    
    // poisoning propagates
    let handle = thread::spawn(|| {
        INIT.call_once(|| {});
    });
    assert!(handle.join().is_err());
    
    // call_once_force will still run and reset the poisoned state
    INIT.call_once_force(|state| {
        assert!(state.is_poisoned());
    });
    
    // once any success happens, we stop propagating the poison
    INIT.call_once(|| {});
}
