#![feature(explicit_tail_calls)]
#![allow(incomplete_features)]

macro_rules! jump {
    ($target:ident, $flag:expr, $value:expr) => { become $target($flag, $value) };
}
#[inline(never)]
fn plus(_: bool, value: u64) -> u64 { value + 11 }
#[inline(never)]
fn minus(_: bool, value: u64) -> u64 { value - 3 }
#[inline(never)]
fn named(flag: bool, value: u64) -> u64 {
    if flag { jump!(plus, flag, value) } else { jump!(plus, flag, value) }
}
#[inline(never)]
fn indirect(flag: bool, value: u64) -> u64 {
    let target = std::hint::black_box(plus as fn(bool, u64) -> u64);
    if flag { jump!(target, flag, value) } else { jump!(target, flag, value) }
}
#[inline(never)]
fn distinct(flag: bool, value: u64) -> u64 {
    if flag { jump!(plus, flag, value) } else { jump!(minus, flag, value) }
}
#[inline(never)]
fn routed(flag: bool, left: u64, right: u64) -> u64 {
    become std::hint::select_unpredictable(flag, left, right)
}
fn main() {
    for flag in [false, true] {
        let flag = std::hint::black_box(flag);
        let value = std::hint::black_box(37);
        assert_eq!(named(flag, value), 48);
        assert_eq!(indirect(flag, value), 48);
        assert_eq!(distinct(flag, value), if flag { 48 } else { 34 });
        assert_eq!(routed(flag, value, value + 5), if flag { value } else { value + 5 });
    }
}
