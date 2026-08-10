// Extracted from library/std/src/thread/local.rs:143
#![allow(unused)]
fn main() {
    use std::cell::{Cell, RefCell};

    thread_local! {
        pub static FOO: Cell<u32> = const { Cell::new(1) };

        static BAR: RefCell<Vec<f32>> = RefCell::new(vec![1.0, 2.0]);
    }

    assert_eq!(FOO.get(), 1);
    BAR.with_borrow(|v| assert_eq!(v[1], 2.0));
}
