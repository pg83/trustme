// Extracted from library/std/src/thread/local.rs:428
#![allow(unused)]
fn main() {
    use std::cell::Cell;

    thread_local! {
        static X: Cell<Option<i32>> = const { Cell::new(Some(1)) };
    }

    assert_eq!(X.take(), Some(1));
    assert_eq!(X.take(), None);
}
