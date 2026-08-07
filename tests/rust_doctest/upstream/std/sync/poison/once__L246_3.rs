// Extracted from library/std/src/sync/poison/once.rs:246
#![allow(unused)]
fn main() {
    use std::sync::Once;
    use std::thread;

    static INIT: Once = Once::new();

    assert_eq!(INIT.is_completed(), false);
    let handle = thread::spawn(|| {
        INIT.call_once(|| panic!());
    });
    assert!(handle.join().is_err());
    assert_eq!(INIT.is_completed(), false);
}
