#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn number<const N: usize>() -> usize { N }

trait Width: Sized {
    fn width() -> usize
    where
        [(); std::mem::size_of::<Self>()]:,
    {
        const { number::<{ std::mem::size_of::<Self>() }>() }
    }
}

impl Width for u16 {}
impl Width for [u16; 3] {}

fn main() {
    assert_eq!(u16::width(), 2);
    assert_eq!(<[u16; 3]>::width(), 6);
}
