//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(specialization)]
#![allow(dead_code, incomplete_features)]

trait Mark {}
trait Project { type Out: Mark; }
struct S<T>(T);

impl<T: Mark> S<T> {
    fn typed<A>(&self) -> impl Sized { 0u8 }
    fn valued<const N: usize>(&self) -> impl Sized { 0u8 }
}
impl S<u8> {
    fn typed(&self) -> impl Sized { () }
    fn valued(&self) -> impl Sized { () }
}

struct Pair<A, B>(A, B);
trait Pick { fn pick(&self); }
impl<A, B> Pick for Pair<A, B> { default fn pick(&self) {} }
impl<T> Pick for Pair<T, T> { fn pick(&self) {} }

fn exercise<T: Project>(generic: &S<T::Out>, concrete: &S<u8>) {
    Pair(generic.typed::<u32>(), concrete.typed()).pick();
    Pair(generic.valued::<3>(), concrete.valued()).pick();
}
