//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(builtin_syntax)]

pub trait Source {
    type At<const N: usize>;
}

pub struct Wrap<T, const N: usize>(T);

pub trait Relay<X> {
    type Out;
}

impl<T: Source, const N: usize> Relay<T::At<N>> for Wrap<T, N> {
    type Out = T::At<N>;
}

fn pointer<T: Source, const N: usize, X>()
    -> fn(T, X, <Wrap<T, N> as Relay<X>>::Out, [(); N])
where
    Wrap<T, N>: Relay<X>,
{
    loop {}
}

type Same<T, X> = fn(T, X, X, [(); 7]);

pub fn relate<T: Source>() {
    let _ = builtin # type_ascribe(pointer::<_, _, _>(), Same<T, _>);
}
