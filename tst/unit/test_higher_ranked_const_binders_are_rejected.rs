//@ compile-fail: Unexpected token
#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]
trait At<const N:usize> { fn value() -> usize; }
struct All;
impl<const N:usize> At<N> for All { fn value() -> usize { N } }
fn sum<T>() -> usize where for<const N:usize> T: At<N> { <T as At<3>>::value()+<T as At<9>>::value() }
fn main() { assert_eq!(sum::<All>(),12); }
