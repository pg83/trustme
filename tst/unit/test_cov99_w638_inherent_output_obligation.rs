//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

trait Source {
    type Item;
}

impl Source for () {
    type Item = u8;
}

struct Bound<T>(std::marker::PhantomData<T>);

impl<T, U> Bound<T>
where
    T: Source<Item = U>,
{
    type Item = U;
}

struct Maker;

impl Maker {
    fn inspect<I, F>(&self, _f: F)
    where
        I: Iterator<Item = Bound<()>::Item>,
        F: FnOnce(I),
    {
    }
}

fn main() {
    Maker.inspect(|iter| {
        let _: std::iter::Empty<u8> = iter;
    });
}
