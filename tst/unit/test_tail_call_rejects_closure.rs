//@ compile-fail: tail calls can only be performed with function definitions or pointers
#![feature(explicit_tail_calls, fn_traits, unboxed_closures)]
#![allow(incomplete_features)]
fn jump(value: u32) -> u32 {
    let increment = |value: u32| value + 1;
    become increment(value)
}
fn main() {}
