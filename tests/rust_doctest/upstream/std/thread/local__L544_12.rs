// Extracted from library/std/src/thread/local.rs:544
#![allow(unused)]
fn main() {
    use std::cell::RefCell;
    
    thread_local! {
        static X: RefCell<Vec<i32>> = RefCell::new(Vec::new());
    }
    
    X.with_borrow_mut(|v| v.push(1));
    
    X.with_borrow(|v| assert_eq!(*v, vec![1]));
}
