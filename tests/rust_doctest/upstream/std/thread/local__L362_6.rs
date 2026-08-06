// Extracted from library/std/src/thread/local.rs:362
#![allow(unused)]
fn main() {
    use std::cell::Cell;
    
    thread_local! {
        static X: Cell<i32> = panic!("!");
    }
    
    // Calling X.get() here would result in a panic.
    
    X.set(123); // But X.set() is fine, as it skips the initializer above.
    
    assert_eq!(X.get(), 123);
}
