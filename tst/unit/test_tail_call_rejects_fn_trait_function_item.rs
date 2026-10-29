//@ compile-fail: tail calls can only be performed with function definitions or pointers
#![feature(explicit_tail_calls, fn_traits, unboxed_closures)]
#![allow(incomplete_features)]
fn step(value: u32) -> u32 { value + 1 }
fn jump(value: u32) -> u32 {
    become FnOnce::call_once(step, (value,))
}
fn main() {}
