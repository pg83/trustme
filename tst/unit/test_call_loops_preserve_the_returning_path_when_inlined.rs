//@ compile-variant: -O
//@ compile-variant: -O -Cpanic=abort
extern "C" { fn putchar(value: i32) -> i32; }

fn direct(value: i32) -> ! {
    loop { unsafe { putchar(value); } }
}

fn indirect(value: i32, call: unsafe extern "C" fn(i32) -> i32) -> ! {
    loop { unsafe { call(value); } }
}

#[inline(never)]
fn choose(stop: bool, value: i32) -> i32 {
    if stop {
        if value & 1 == 0 { direct(value) } else { indirect(value, putchar) }
    }
    value ^ 0x53
}

fn main() {
    for value in [17, 32, 61] {
        assert_eq!(choose(std::hint::black_box(false), value), value ^ 0x53);
    }
}
