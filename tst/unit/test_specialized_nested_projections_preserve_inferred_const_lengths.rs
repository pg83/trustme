#![feature(specialization)]
#![allow(incomplete_features)]
//@ compile-fail: error:0: Failed to find impl with 'filled'
use std::marker::PhantomData;

struct Packet<const N: usize>([u16; N]);

impl<const N: usize> Packet<N> {
    fn filled(value: u16) -> Self {
        Packet([value; N])
    }
}

trait Echo<const N: usize> {
    type Output;
}

struct Tag<T>(PhantomData<T>);

impl<T, const N: usize> Echo<N> for Tag<T> {
    default type Output = [u8; N];
}

impl<const N: usize> Echo<N> for Tag<u16> {
    type Output = Packet<N>;
}

trait Make<const N: usize> {
    type Output;
}

struct Factory<T>(PhantomData<T>);

impl<T, const N: usize> Make<N> for Factory<T> {
    type Output = <Tag<T> as Echo<N>>::Output;
}

fn main() {
    let short = <Factory<u16> as Make<_>>::Output::filled(757);
    let short: Packet<2> = short;
    assert_eq!(short.0, [757, 757]);

    let long = <Factory<u16> as Make<_>>::Output::filled(761);
    let long: Packet<3> = long;
    assert_eq!(long.0, [761, 761, 761]);
}
