// The other half of `in_external_macro`: a token a proc macro hands back from
// its input keeps its place in the crate's own code, so `unsafe_code` sees it.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
//@ compile-fail: usage of an `unsafe` block
#![deny(unsafe_code)]

use proc_macro_item_passthrough::echo_item;

#[echo_item]
fn read(p: *const u8) -> u8 {
    unsafe { *p }
}

fn main() {
    let x = 5u8;
    assert_eq!(read(&x), 5);
}
