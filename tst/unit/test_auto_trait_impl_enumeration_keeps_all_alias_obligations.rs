#![feature(auto_traits, inherent_associated_types)]

auto trait Label {}
struct First;
struct Second;
struct Generic<T>(std::marker::PhantomData<T>);
impl Label for First {}
impl Label for Second {}
impl<T> Label for Generic<T> {}

struct Container<T>(T);
impl<T: Label> Container<T> { type Output = u16; }
trait Select { type Value: Label; }
struct A;
struct B;
struct C;
impl Select for A { type Value = First; }
impl Select for B { type Value = Second; }
impl Select for C { type Value = Generic<u8>; }
type Projected<T: Select> = Container<T::Value>::Output;

fn main() {
    let first: Projected<A> = 257;
    let second: Projected<B> = 513;
    let generic: Projected<C> = 1027;
    assert_eq!((first, second, generic), (257, 513, 1027));
}
