#![feature(explicit_tail_calls)]
#![allow(incomplete_features)]

#[inline(never)]
fn finish<T: Copy>(_: bool, value: T) -> T { value }

#[inline]
pub fn direct<T: Copy>(flag: bool, value: T) -> T {
    become finish(flag, value)
}

#[inline(never)]
fn increase<const N: u16>(_: bool, value: u16) -> u16 { value + N }

#[inline(never)]
fn decrease<const N: u16>(_: bool, value: u16) -> u16 { value - N }

#[inline]
pub fn indirect<const N: u16>(flag: bool, value: u16) -> u16 {
    let next: fn(bool, u16) -> u16 = if flag { increase::<N> } else { decrease::<N> };
    become next(flag, value)
}
