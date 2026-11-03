//@ compile-variant: -O
#![feature(core_intrinsics)]
#[inline(never)]
fn sink() -> ! { loop { std::hint::spin_loop(); } }
#[inline]
fn forward() -> ! { sink() }
#[inline]
fn through_pointer(call: fn() -> !) -> ! { call() }
#[inline]
fn fatal() -> ! { std::intrinsics::abort() }
fn choose(mode: u8) -> u16 {
    match mode {
        1 => forward(),
        2 => through_pointer(sink),
        3 => fatal(),
        _ => 137,
    }
}
fn main() {
    assert_eq!(choose(std::hint::black_box(0)), 137);
    let wrappers: [fn() -> !; 2] = [forward, fatal];
    assert_eq!(wrappers.len(), 2);
}
