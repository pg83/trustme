//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(specialization)]
#![allow(incomplete_features)]

trait Mark {}
trait Project { type Out: Mark; }
struct S<T>(T);
impl<T: Mark> S<T> { fn probe<A>() {} }
impl S<u8> { fn probe() {} }

struct Pair<A, B>(A, B);
trait Pick { fn pick(&self); }
impl<A, B> Pick for Pair<A, B> { default fn pick(&self) {} }
impl<T> Pick for Pair<T, T> { fn pick(&self) {} }

fn exercise<T: Project>() {
    Pair(S::<T::Out>::probe::<u32>, S::<u8>::probe).pick();
}
