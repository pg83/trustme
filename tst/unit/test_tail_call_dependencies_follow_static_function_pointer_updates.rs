//@ compile-variant: -O
#![feature(explicit_tail_calls)]
static OFFSET: u16 = 7;
fn add(left: u16, right: u16) -> u16 { left + right + OFFSET }
fn multiply(left: u16, right: u16) -> u16 { left * right + OFFSET }
static mut TARGET: fn(u16, u16) -> u16 = add;
#[inline(never)]
fn relay(left: u16, right: u16) -> u16 {
    let target = unsafe { TARGET };
    become target(left, right)
}
fn main() {
    assert_eq!(relay(3, 5), 15);
    unsafe { TARGET = multiply; }
    assert_eq!(relay(11, 13), 150);
}
