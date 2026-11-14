//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(builtin_syntax)]

pub trait Source {
    type At<const N: usize>;
}

struct First;
struct Second;
struct Wrap<T, K, const N: usize>(T, K);

trait Relay<X> {
    type Out;
}

impl<T: Source, const N: usize> Relay<T::At<N>> for Wrap<T, First, N> {
    type Out = T::At<N>;
}

impl<T, const N: usize> Relay<u8> for Wrap<T, Second, N> {
    type Out = u16;
}

fn pointer<T, K, const N: usize, X>()
    -> fn(K, X, <Wrap<T, K, N> as Relay<X>>::Out, [(); N])
where
    Wrap<T, K, N>: Relay<X>,
{
    loop {}
}

type Same<X> = fn(First, X, X, [(); 7]);

pub fn relate<T: Source>() {
    let _ = builtin # type_ascribe(pointer::<T, _, _, _>(), Same<_>);
}
