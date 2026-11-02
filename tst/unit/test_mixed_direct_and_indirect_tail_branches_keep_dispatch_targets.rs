//@ compile-variant: -O
#![feature(explicit_tail_calls)]

fn direct(mode: u8, value: u32) -> u32 { value.wrapping_add(17 + mode as u32) }
fn indirect(mode: u8, value: u32) -> u32 { value.wrapping_mul(3).wrapping_add(mode as u32) }
static mut TARGET: fn(u8, u32) -> u32 = indirect;

#[inline(never)]
fn route(mode: u8, value: u32) -> u32 {
    let target = unsafe { TARGET };
    match mode {
        0 => become direct(mode, value),
        1 => become target(mode, value),
        _ => become target(mode, value),
    }
}

fn main() {
    assert_eq!(route(0, 11), 28);
    assert_eq!(route(1, 11), 34);
    assert_eq!(route(2, 11), 35);
    unsafe { TARGET = direct; }
    assert_eq!(route(1, 11), 29);
    assert_eq!(route(2, 11), 30);
}
