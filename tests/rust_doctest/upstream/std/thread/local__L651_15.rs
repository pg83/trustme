// Extracted from library/std/src/thread/local.rs:651
#![allow(unused)]
fn main() {
    use std::cell::RefCell;

    thread_local! {
        static X: RefCell<Vec<i32>> = RefCell::new(Vec::new());
    }

    let prev = X.replace(vec![1, 2, 3]);
    assert!(prev.is_empty());

    X.with_borrow(|v| assert_eq!(*v, vec![1, 2, 3]));
}
