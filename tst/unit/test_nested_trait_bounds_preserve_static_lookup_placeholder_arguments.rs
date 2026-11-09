use std::marker::PhantomData;

trait Permit<T: ?Sized, const N: usize> {}

impl<T: ?Sized, const N: usize> Permit<T, N> for () {}

trait Family<T: ?Sized, const N: usize> {
    type Output;
}

struct Maker;

struct Envelope<T: ?Sized, const N: usize>([u8; N], PhantomData<*const T>);

impl<T: ?Sized, const N: usize> Family<T, N> for Maker
where
    (): Permit<T, N>,
{
    type Output = Envelope<T, N>;
}

impl<T: ?Sized, const N: usize> Envelope<T, N> {
    fn build(_source: &T, bytes: [u8; N]) -> Self {
        Self(bytes, PhantomData)
    }
}

fn main() {
    let value = <Maker as Family<_, _>>::Output::build(&13u16, [41, 43, 47]);
    assert_eq!(value.0, [41, 43, 47]);
}
