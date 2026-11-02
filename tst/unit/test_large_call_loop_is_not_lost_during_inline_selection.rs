//@ compile-variant: -O
//@ compile-variant: -O -Cpanic=abort
extern "C" { fn printf(format: *const i8, ...) -> i32; }
const FORMAT: *const i8 = b"%d%d%d%d%d%d%d%d%d%d%d%d\0" as *const u8 as *const i8;

fn large(value: i32) -> ! {
    loop {
        unsafe {
            printf(FORMAT, value ^ 1, value ^ 2, value ^ 3, value ^ 4,
                value ^ 5, value ^ 6, value ^ 7, value ^ 8,
                value ^ 9, value ^ 10, value ^ 11, value ^ 12);
        }
    }
}

#[inline(never)]
fn choose(stop: bool, value: i32) -> i32 {
    if stop { large(value) }
    value ^ 0x71
}

fn main() {
    assert_eq!(choose(std::hint::black_box(false), 19), 19 ^ 0x71);
}
