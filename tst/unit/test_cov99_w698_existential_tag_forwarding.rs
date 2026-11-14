//@ compile-only
//@ compile-flags: --emit=metadata
//@ compile-fail: error:

use core::marker::PhantomData;

trait Feed {
    type Elem;
}

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
struct Test<S>(PhantomData<S>);

impl<S, U> Gate for Test<S>
where
    S: Feed<Elem = Wrapper<U, u8>>,
    Wrapper<U, u8>: Outer<Output = u32>,
{}

fn demand<T: Gate>() {}

fn request<S: Feed>() {
    demand::<Test<S>>();
}

fn main() {}
