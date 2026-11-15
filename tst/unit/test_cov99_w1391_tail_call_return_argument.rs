//@ edition: 2021
//@ compile-flags: -Cpanic=abort
#![feature(explicit_tail_calls)]
#![allow(unreachable_code)]

fn callee(value: u32) -> u32 {
    value + 1
}

fn through_argument(value: u32) -> u32 {
    become callee(return value);
}

fn main() {
    assert_eq!(through_argument(41), 41);
}
