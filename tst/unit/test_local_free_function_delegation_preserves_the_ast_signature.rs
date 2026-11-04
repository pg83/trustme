//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]

fn original(value: (u16, u16)) -> (u16, u16) { (value.1, value.0) }
reuse crate::original as forwarded;

fn main() {
    assert_eq!(forwarded((181, 191)), (191, 181));
}
