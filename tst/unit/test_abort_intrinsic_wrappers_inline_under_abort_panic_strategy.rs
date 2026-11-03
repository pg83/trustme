//@ compile-variant: -O -C panic=abort
#![feature(core_intrinsics)]
#[inline]
fn stop() -> ! { std::intrinsics::abort() }
#[inline]
fn forward() -> ! { stop() }
fn main() {
    if std::hint::black_box(false) { forward(); }
    assert_eq!(std::hint::black_box(97u32), 97);
}
