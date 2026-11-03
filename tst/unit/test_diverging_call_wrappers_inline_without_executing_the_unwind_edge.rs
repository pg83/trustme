//@ compile-variant: -O
extern "C" { fn abort() -> !; }
#[inline]
fn stop() -> ! { unsafe { abort() } }
#[inline]
fn forward() -> ! { stop() }
#[inline]
fn choose(stop_now: bool) -> u32 { if stop_now { forward() } else { 83 } }
fn main() {
    assert_eq!(choose(std::hint::black_box(false)), 83);
    let selected: fn() -> ! = forward;
    assert_ne!(selected as usize, 0);
}
