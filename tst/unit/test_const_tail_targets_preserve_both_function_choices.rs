//@ compile-flags: -Zunleash-the-miri-inside-of-you -Zmir-opt-level=0
#![feature(explicit_tail_calls)]
#![allow(incomplete_features)]

const fn subtract(_: bool, value: u16) -> u16 { value - 5 }
const fn multiply(_: bool, value: u16) -> u16 { value * 3 }

const fn direct(flag: bool, value: u16) -> u16 {
    if flag { become multiply(flag, value) } else { become subtract(flag, value) }
}

const fn indirect(flag: bool, value: u16) -> u16 {
    let next: fn(bool, u16) -> u16 = if flag { subtract } else { multiply };
    become next(flag, value)
}

const ANSWERS: [u16; 4] = [direct(true, 11), direct(false, 19), indirect(true, 29), indirect(false, 7)];

fn main() { assert_eq!(ANSWERS, [33, 14, 24, 21]); }
