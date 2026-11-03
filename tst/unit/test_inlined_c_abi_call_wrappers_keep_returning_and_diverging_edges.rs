//@ compile-variant: -O
//@ compile-variant: -Cpanic=abort
unsafe extern "C" { fn abs(value: i32) -> i32; fn abort() -> !; }
#[inline]
fn absolute(value: i32) -> i32 { unsafe { abs(value) } }
#[inline]
fn stop() -> ! { unsafe { abort() } }
#[inline]
fn invoke(stop: unsafe extern "C" fn() -> !) -> ! { unsafe { stop() } }
fn choose(value: i32, halt: bool) -> i32 {
    if halt {
        if value == 0 { stop() } else { invoke(abort) }
    }
    absolute(value)
}
fn main() {
    assert_eq!(choose(std::hint::black_box(-401), std::hint::black_box(false)), 401);
    assert_eq!(choose(std::hint::black_box(409), std::hint::black_box(false)), 409);
}
