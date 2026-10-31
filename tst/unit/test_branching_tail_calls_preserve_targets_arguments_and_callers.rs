#![feature(explicit_tail_calls)]
#[inline(never)]
fn plus(_: bool, value: u32) -> u32 { value + 7 }
#[inline(never)]
fn times(_: bool, value: u32) -> u32 { value * 3 }
#[inline(never)]
fn choose(flag: bool, value: u32) -> u32 {
    if flag { become plus(flag, value) } else { become times(flag, value) }
}
#[inline(never)]
fn equal(flag: bool, value: u32) -> u32 {
    if flag { become plus(flag, value) } else { become plus(flag, value) }
}
fn main() {
    assert_eq!(choose(std::hint::black_box(true), 11), 18);
    assert_eq!(choose(std::hint::black_box(false), 11), 33);
    assert_eq!(equal(std::hint::black_box(true), 11), 18);
    assert_eq!(equal(std::hint::black_box(false), 11), 18);
}
