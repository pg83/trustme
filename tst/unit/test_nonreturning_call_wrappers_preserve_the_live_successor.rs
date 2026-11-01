//@ compile-variant: -O
//@ compile-variant: -Cpanic=abort
unsafe extern "C" { fn abort() -> !; }

#[inline]
fn direct() -> ! { unsafe { abort() } }
#[inline]
fn indirect(call: unsafe extern "C" fn() -> !) -> ! { unsafe { call() } }
#[inline]
fn forwarded(call: fn() -> !) -> ! { call() }

fn choose(which: u8, stop: bool) -> u16 {
    if stop {
        match which {
            0 => direct(),
            1 => indirect(abort),
            _ => forwarded(direct),
        }
    }
    31 + u16::from(which)
}

fn main() {
    for which in 0..3 {
        assert_eq!(choose(std::hint::black_box(which), std::hint::black_box(false)), 31 + u16::from(which));
    }
}
