//@ compile-fail: Failed to locate function
use std::marker::PhantomData;
struct Count<const N: usize>;
struct Pair<A, B>(PhantomData<(A, B)>);
impl<T> Pair<T, T> { fn code() -> u8 { 7 } }
fn wrong() -> u8 { Pair::<Count<3>, Count<4>>::code() }
fn main() {}
