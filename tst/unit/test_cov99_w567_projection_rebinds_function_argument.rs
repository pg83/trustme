//@ crate-type: lib

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

fn projected<T: Source, const N: usize, X>(
    _: T,
    _: X,
    _: <Wrap<T, N> as Relay<X>>::Out,
    _: [(); N],
) where
    Wrap<T, N>: Relay<X>,
{
}

fn same<T, X>(_: fn(T, X, X, [(); 7])) {}

pub fn relate<T: Source>() {
    same::<T, _>(projected::<_, _, _>);
}
