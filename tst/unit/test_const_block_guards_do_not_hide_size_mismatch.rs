//@ compile-fail: Value mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn require_width(n:usize) { assert!(n>0); }
fn lengthen<const N:usize>(value:[u8;{require_width(N);N+1}])->[u8;{require_width(N+1);N+2}]
where [();{require_width(N);N+1}]:, [();{require_width(N+1);N+2}]: {
 value
}
fn main() {}
