// Extracted from library/std/src/thread/local.rs:167
#![allow(unused)]
fn main() {
    use std::cell::RefCell;

    thread_local! {
        pub static FOO: RefCell<Vec<u32>> = const { RefCell::new(Vec::new()) };
    }

    FOO.with_borrow(|v| assert_eq!(v.len(), 0));
}
