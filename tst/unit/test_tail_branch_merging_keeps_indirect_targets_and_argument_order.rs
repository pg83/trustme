//@ compile-variant: -O
#![feature(explicit_tail_calls)]
fn three(_: u8, left: u32, right: u32) -> u32 { left.wrapping_mul(3).wrapping_add(right) }
fn five(_: u8, left: u32, right: u32) -> u32 { left.wrapping_mul(5).wrapping_add(right) }
#[inline(never)]
fn dispatch(mode: u8, left: u32, right: u32) -> u32 {
    let first = std::hint::black_box(three as fn(u8, u32, u32) -> u32);
    let second = std::hint::black_box(five as fn(u8, u32, u32) -> u32);
    match mode {
        0 => become first(mode, left, right),
        1 => become second(mode, left, right),
        _ => become first(mode, right, left),
    }
}
fn main() {
    assert_eq!(dispatch(0, 23, 7), 76);
    assert_eq!(dispatch(1, 23, 7), 122);
    assert_eq!(dispatch(2, 23, 7), 44);
}
