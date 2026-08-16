// A negative bound states that a trait is *not* implemented. As a requirement
// on the implementor it is vacuous, so it is parsed and dropped -- but it was
// not parsed at all, and the `!` was an unexpected token.
//
// Same shape as the upstream test traits/negative-bounds/supertrait.rs.
#![feature(negative_bounds)]
#![allow(dead_code)]

trait A: !B {}
trait B: !A {}

trait C {}
trait D {}

// Alongside an ordinary supertrait, on either side of it.
trait E: C + !D {}
trait F: !D + C {}

fn takesC<T>(_: T) -> u32
where
    T: C,
{
    1
}

struct S;
impl C for S {}

fn main() {
    assert_eq!(takesC(S), 1);
}
