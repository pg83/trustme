// Extracted from library/std/src/sync/poison/once.rs:234
#![allow(unused)]
fn main() {
    use std::sync::Once;

    static INIT: Once = Once::new();

    assert_eq!(INIT.is_completed(), false);
    INIT.call_once(|| {
        assert_eq!(INIT.is_completed(), false);
    });
    assert_eq!(INIT.is_completed(), true);
}
