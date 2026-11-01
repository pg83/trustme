#![feature(core_intrinsics, const_eval_select)]
#![allow(internal_features)]
#[derive(Copy, Clone)]
struct Pair(u8, u32);
const fn compile(value: Pair, extra: u16) -> u32 { value.0 as u32 + value.1 + extra as u32 }
fn runtime(value: Pair, extra: u16) -> u32 { compile(value, extra) + 1000 }
const VALUE: u32 = core::intrinsics::const_eval_select((Pair(17, 70000), 31), compile, runtime);
fn main() {
    assert_eq!(VALUE, 70048);
    assert_eq!(core::intrinsics::const_eval_select((Pair(17, 70000), 31), compile, runtime), 71048);
}
