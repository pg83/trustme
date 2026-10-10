// generic-array's `flat.unflatten()` under `impl<T, NM, N> Unflatten<T, NM,
// N> for GenericArray<T, NM> where NM: Div<N>, N: ArrayLength, Quot<NM, N>:
// ArrayLength`, inside `assert_eq!` - no expected type at the lookup. The
// one `Div` impl for a non-zero numerator makes `N = UInt<?Ur, ?Br>`, and
// `Quot<U6, UInt<?Ur, ?Br>>` normalizes only as far as a projection that
// waits on a comparison of the unknowns. Upstream treats a projection that
// mentions inference variables as not yet normalized - a goal on it is
// ambiguous. We took a projection whose self type was known for rigid,
// found no impl of `ArrayLength` for it, and rejected the method.
use std::marker::PhantomData;

pub trait Halve<D> {
    type Out;
}

pub struct U<A>(PhantomData<A>);
pub struct T0;

impl<A, B> Halve<U<B>> for U<A>
where
    (): PHalve<A, B>,
{
    type Out = <() as PHalve<A, B>>::Q;
}

pub trait PHalve<A, B> {
    type Q;
}

impl PHalve<T0, T0> for () {
    type Q = T0;
}

impl<A> PHalve<U<A>, T0> for () {
    type Q = U<A>;
}

impl<A, B> PHalve<U<A>, U<B>> for ()
where
    (): PHalve<A, B>,
{
    type Q = <() as PHalve<A, B>>::Q;
}

impl Halve<T0> for T0 {
    type Out = T0;
}

pub trait Len {}
impl Len for T0 {}
impl<A: Len> Len for U<A> {}

pub struct Arr<T, N>(PhantomData<(T, N)>);

pub trait Unflat<N> {
    type Output;
    fn unflat(self) -> Self::Output;
}

impl<T, NM, N> Unflat<N> for Arr<T, NM>
where
    NM: Len + Halve<N>,
    N: Len,
    <NM as Halve<N>>::Out: Len,
{
    type Output = Arr<Arr<T, N>, <NM as Halve<N>>::Out>;
    fn unflat(self) -> Self::Output {
        Arr(PhantomData)
    }
}

fn same<X>(_: &X, _: &X) {}

fn main() {
    let a: Arr<i32, U<U<T0>>> = Arr(PhantomData);
    let expected: Arr<Arr<i32, U<U<T0>>>, T0> = Arr(PhantomData);
    same(&a.unflat(), &expected);
}
