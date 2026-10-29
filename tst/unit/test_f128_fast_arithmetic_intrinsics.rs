#![feature(core_intrinsics, f128)]
#[inline(never)]
fn arithmetic(left: f128, right: f128) -> [f128; 4] {
    unsafe { [
        core::intrinsics::fadd_fast(left, right),
        core::intrinsics::fsub_fast(left, right),
        core::intrinsics::fmul_fast(left, right),
        core::intrinsics::fdiv_fast(left, right),
    ] }
}
fn main() {
    assert_eq!(arithmetic(7.5, 2.5), [10.0, 5.0, 18.75, 3.0]);
    assert_eq!(arithmetic(-7.5, 2.0), [-5.5, -9.5, -15.0, -3.75]);
}
