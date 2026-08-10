// Extracted from library/core/src/mem/manually_drop.rs:167
#![allow(unused)]
fn main() {
    use std::mem::ManuallyDrop;
    let mut x = ManuallyDrop::new(String::from("Hello World!"));
    x.truncate(5); // You can still safely operate on the value
    assert_eq!(*x, "Hello");
    // But `Drop` will not be run here
    // FIXME(https://github.com/rust-lang/miri/issues/3670):
    // use -Zmiri-disable-leak-check instead of unleaking in tests meant to leak.
    let _ = ManuallyDrop::into_inner(x);
}
