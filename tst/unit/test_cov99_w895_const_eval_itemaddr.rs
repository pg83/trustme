//@ compile-flags: -Z mir-opt-level=1 -Z dump-mir -C panic=abort
#![feature(core_intrinsics, const_eval_select)]

const fn constant_value(value: u32) -> u32 {
    value + 1
}

fn runtime_value(value: u32) -> u32 {
    value + 2
}

const CALLBACK: fn(u32) -> u32 = runtime_value;

fn main() {
    let selected = unsafe {
        core::intrinsics::const_eval_select((40u32,), constant_value, CALLBACK)
    };
    assert_eq!(selected, 42);
}
