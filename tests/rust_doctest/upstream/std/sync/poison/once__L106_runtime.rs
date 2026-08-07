// Extracted from library/std/src/sync/poison/once.rs:106
#![allow(unused)]
fn main() {
    use std::sync::Once;

    static mut VAL: usize = 0;
    static INIT: Once = Once::new();

    // Accessing a `static mut` is unsafe much of the time, but if we do so
    // in a synchronized fashion (e.g., write once or read all) then we're
    // good to go!
    //
    // This function will only call `expensive_computation` once, and will
    // otherwise always return the value returned from the first invocation.
    fn get_cached_val() -> usize {
        unsafe {
            INIT.call_once(|| {
                VAL = expensive_computation();
            });
            VAL
        }
    }

    fn expensive_computation() -> usize {
        // ...
    2
    }
}
