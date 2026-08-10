// Extracted from library/std/src/thread/local.rs:478
#![allow(unused)]
#![feature(local_key_cell_update)]
fn main() {
    use std::cell::Cell;

    thread_local! {
        static X: Cell<i32> = const { Cell::new(5) };
    }

    X.update(|x| x + 1);
    assert_eq!(X.get(), 6);
}
