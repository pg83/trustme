//@ compile-variant: -O
unsafe extern "C" { fn abort() -> !; }
#[inline(never)]
fn stop() -> ! { unsafe { abort() } }
#[inline(never)]
fn route(stop_now: bool, value: u32) -> u32 {
    if stop_now { stop() }
    value.wrapping_mul(3).wrapping_add(7)
}
fn main() {
    let stop_now = std::hint::black_box(false);
    for value in [0u32, 13, u32::MAX] {
        assert_eq!(route(stop_now, value), value.wrapping_mul(3).wrapping_add(7));
    }
}
