// Extracted from library/std/src/thread/local.rs:399
#![allow(unused)]
fn main() {
    use std::cell::Cell;
    
    thread_local! {
        static X: Cell<i32> = const { Cell::new(1) };
    }
    
    assert_eq!(X.get(), 1);
}
