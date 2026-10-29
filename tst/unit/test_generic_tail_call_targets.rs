//@ compile-flags: -Zmir-opt-level=0
#![feature(explicit_tail_calls)]
#![allow(incomplete_features)]
#[inline(never)]
fn widen<T: Into<u64>>(value: T) -> u64 { value.into() + 2 }
#[inline(never)]
fn direct<T: Into<u64>>(value: T) -> u64 { become widen::<T>(value) }
#[inline(never)]
fn indirect<T: Into<u64>>(value: T) -> u64 {
    let next: fn(T) -> u64 = widen::<T>;
    become next(value)
}
fn main() {
    assert_eq!(direct(std::hint::black_box(7u16)), 9);
    assert_eq!(indirect(std::hint::black_box(11u32)), 13);
}
