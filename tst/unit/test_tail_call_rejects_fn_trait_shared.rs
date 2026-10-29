//@ compile-fail: tail calls can only be performed with function definitions or pointers
#![feature(explicit_tail_calls, fn_traits, unboxed_closures)]
#![allow(incomplete_features)]
fn jump(mut operation: fn(u32) -> u32, value: u32) -> u32 {
    become <fn(u32) -> u32 as Fn<(u32,)>>::call(&operation, (value,))
}
fn main() {}
