//@ compile-only
//@ compile-flags: --emit=metadata
//@ compile-fail: error:

use core::marker::PhantomData;

trait Inner {
    type Output;
}

struct Probe<T>(PhantomData<T>);
impl Inner for Probe<u8> {
    type Output = bool;
}

trait Outer {
    type Output;
}

struct Wrapper<Tag, T>(PhantomData<(Tag, T)>);

impl<T> Outer for Wrapper<u8, T>
where
    Probe<T>: Inner,
{
    type Output = <Probe<T> as Inner>::Output;
}

impl<T> Outer for Wrapper<u16, T>
where
    Probe<T>: Inner,
{
    type Output = <Probe<T> as Inner>::Output;
}

trait Gate {}
struct Test<T>(PhantomData<T>);
impl<T: Outer<Output = u32>> Gate for Test<T> {}

fn demand<T: Gate>(_: T) {}

fn main() {
    demand(Test::<Wrapper<_, u8>>(PhantomData));
}
