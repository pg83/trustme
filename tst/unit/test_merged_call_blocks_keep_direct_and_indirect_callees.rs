//@ compile-variant: -O
#[inline(never)]
fn three(value: u32) -> u32 { value.wrapping_mul(3).wrapping_add(1) }
#[inline(never)]
fn five(value: u32) -> u32 { value.wrapping_mul(5).wrapping_add(2) }
#[inline(never)]
fn direct(first: bool, value: u32) -> u32 { if first { three(value) } else { five(value) } }
#[inline(never)]
fn indirect(first: bool, left: fn(u32) -> u32, right: fn(u32) -> u32, value: u32) -> u32 {
    if first { left(value) } else { right(value) }
}
#[inline(never)]
fn mixed(first: bool, target: fn(u32) -> u32, value: u32) -> u32 {
    if first { three(value) } else { target(value) }
}
fn main() {
    let left = std::hint::black_box(three as fn(u32) -> u32);
    let right = std::hint::black_box(five as fn(u32) -> u32);
    assert_eq!(direct(true, 13), 40);
    assert_eq!(direct(false, 13), 67);
    assert_eq!(indirect(true, left, right, 13), 40);
    assert_eq!(indirect(false, left, right, 13), 67);
    assert_eq!(mixed(true, right, 13), 40);
    assert_eq!(mixed(false, right, 13), 67);
}
