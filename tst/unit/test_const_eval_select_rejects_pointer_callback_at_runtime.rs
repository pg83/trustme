//@ compile-fail: const_eval_select const callback must be a function item
#![feature(core_intrinsics, const_eval_select)]
#![allow(internal_features)]

const fn compile(value: u32) -> u32 { value * 3 + 1 }
fn runtime(value: u32) -> u32 { value * 3 + 1 }

const fn choose(value: u32) -> u32 {
    let callback: fn(u32) -> u32 = compile;
    std::intrinsics::const_eval_select((value,), callback, runtime)
}

fn main() { assert_eq!(choose(7), 22); }
