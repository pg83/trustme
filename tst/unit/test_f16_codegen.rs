#![feature(f16)]
#![allow(invalid_nan_comparisons)]

#[inline(never)]
fn add(left: f16, right: f16) -> f16 {
    left + right
}

#[inline(never)]
fn sub(left: f16, right: f16) -> f16 {
    left - right
}

#[inline(never)]
fn mul(left: f16, right: f16) -> f16 {
    left * right
}

#[inline(never)]
fn div(left: f16, right: f16) -> f16 {
    left / right
}

#[inline(never)]
fn rem(left: f16, right: f16) -> f16 {
    left % right
}

fn main() {
    assert_eq!(add(1.5, 2.25).to_bits(), 0x4380);
    assert_eq!(sub(2.25, 1.5).to_bits(), 0x3a00);
    assert_eq!(mul(1.5, 2.25).to_bits(), 0x42c0);
    assert_eq!(div(2.25, 1.5).to_bits(), 0x3e00);
    assert_eq!(rem(5.5, 2.0).to_bits(), 0x3e00);
    assert_eq!((-1.5f16).to_bits(), 0xbe00);

    assert!(1.5f16 < 2.25);
    assert!(f16::NAN != f16::NAN);
    assert_eq!(4356f16.next_up(), 4360.0);
    assert_eq!((-3.5f16).signum(), -1.0);
    assert_eq!((42u16 as f16).to_bits(), 0x5140);
    assert_eq!(3.75f16 as u16, 3);
    assert_eq!(3.75f16 as f32, 3.75f32);
}
