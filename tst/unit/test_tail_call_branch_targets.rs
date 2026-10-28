#![feature(explicit_tail_calls)]
#![allow(incomplete_features)]

#[inline(never)]
fn add(_: u8, value: i32) -> i32 { value + 7 }

#[inline(never)]
fn subtract(_: u8, value: i32) -> i32 { value - 3 }

#[inline(never)]
fn named(branch: u8, value: i32) -> i32 {
    match branch {
        0 => become add(branch, value),
        1 => become add(branch, value),
        2 => become add(branch, 11),
        _ => become subtract(branch, value),
    }
}

#[inline(never)]
fn indirect(branch: u8, value: i32) -> i32 {
    let first = std::hint::black_box(add as fn(u8, i32) -> i32);
    let second = std::hint::black_box(subtract as fn(u8, i32) -> i32);
    match branch {
        0 => become first(branch, value),
        1 => become first(branch, value),
        2 => become first(branch, 11),
        _ => become second(branch, value),
    }
}

fn main() {
    for (branch, expected) in [12, 12, 18, 2].iter().enumerate() {
        let branch = std::hint::black_box(branch as u8);
        assert_eq!(named(branch, 5), *expected);
        assert_eq!(indirect(branch, 5), *expected);
    }
}
