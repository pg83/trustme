//@ compile-variant: -O
#![feature(explicit_tail_calls)]
fn select<const FIRST: bool>(left: u32, right: u32) -> u32 {
    if FIRST { left } else { right }
}
#[inline(never)]
fn direct<const FIRST: bool>(left: u32, right: u32) -> u32 {
    become select::<FIRST>(left, right)
}
#[inline(never)]
fn indirect<const FIRST: bool>(left: u32, right: u32) -> u32 {
    let target: fn(u32, u32) -> u32 = select::<FIRST>;
    become target(right, left)
}
fn main() {
    assert_eq!(direct::<true>(13, 17), 13);
    assert_eq!(direct::<false>(13, 17), 17);
    assert_eq!(indirect::<true>(19, 23), 23);
    assert_eq!(indirect::<false>(19, 23), 19);
}
