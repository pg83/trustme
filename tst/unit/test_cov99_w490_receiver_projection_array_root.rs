//@ edition: 2021
//@ crate-type: lib
//@ compile-fail: Unknown receiver type
#![feature(arbitrary_self_types)]

struct Wrap<A, B>(A, B);
struct Subject;

trait Assoc {
    type Output;
}

impl Assoc for Subject {
    type Output = ();
}

impl Subject {
    fn invalid(self: Wrap<[Self; 1], <Self as Assoc>::Output>) {}
}
