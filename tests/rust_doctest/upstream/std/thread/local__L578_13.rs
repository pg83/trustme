// Extracted from library/std/src/thread/local.rs:578
#![allow(unused)]
fn main() {
    use std::cell::RefCell;

    thread_local! {
        static X: RefCell<Vec<i32>> = panic!("!");
    }

    // Calling X.with() here would result in a panic.

    X.set(vec![1, 2, 3]); // But X.set() is fine, as it skips the initializer above.

    X.with_borrow(|v| assert_eq!(*v, vec![1, 2, 3]));
}
