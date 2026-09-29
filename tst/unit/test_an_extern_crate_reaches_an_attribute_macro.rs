//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// arc-swap puts `#[rustversion::since(1.36.0)]` on `extern crate alloc;`. The
// item reaches the attribute macro as its tokens like any other item, a
// renaming one included.
use proc_macro_item_passthrough::echo_item;

#[echo_item]
extern crate alloc;

#[echo_item]
extern crate core as renamed_core;

fn main() {
    let v: alloc::vec::Vec<u8> = alloc::vec![1, 2];
    assert_eq!(v.len(), renamed_core::mem::size_of::<u16>());
}
