//@ compile-fail: Value mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn mark() {}
fn lengthen<const N:usize>(value:[u8;{mark();N+1}])->[u8;{mark();N+2}]
where [();{mark();N+1}]:, [();{mark();N+2}]: {
 value
}
fn main() {}
