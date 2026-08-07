// Extracted from library/std/src/sync/poison/once.rs:23
#![allow(unused)]
fn main() {
    use std::sync::Once;

    static START: Once = Once::new();

    START.call_once(|| {
        // run initialization here
    });
}
