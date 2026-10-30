#![feature(core_intrinsics, const_eval_select)]
#![allow(internal_features)]

const fn compile(value: u32) -> u32 { value * 3 + 1 }
fn runtime(value: u32) -> u32 { value * 3 + 1 }

const fn direct(value: u32) -> u32 {
    let callback: fn(u32) -> u32 = runtime;
    std::intrinsics::const_eval_select((value,), compile, callback)
}

const fn indirect(value: u32, callback: fn(u32) -> u32) -> u32 {
    std::intrinsics::const_eval_select((value,), compile, callback)
}

const DIRECT: u32 = direct(7);
const INDIRECT: u32 = indirect(5, runtime);

fn main() {
    assert_eq!(DIRECT, 22);
    assert_eq!(INDIRECT, 16);
    assert_eq!(direct(std::hint::black_box(9)), 28);
    let callback = std::hint::black_box(runtime as fn(u32) -> u32);
    assert_eq!(indirect(11, callback), 34);
}
