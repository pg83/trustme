// Extracted from library/std/src/thread/local.rs:458
#![allow(unused)]
fn main() {
    use std::cell::Cell;

    thread_local! {
        static X: Cell<i32> = const { Cell::new(1) };
    }

    assert_eq!(X.replace(2), 1);
    assert_eq!(X.replace(3), 2);
}
