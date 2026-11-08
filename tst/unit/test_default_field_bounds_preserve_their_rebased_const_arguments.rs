#![feature(default_field_values)]

use std::marker::PhantomData;

trait Seed<const N: usize> {
    const VALUES: [u16; N];
}

impl Seed<2> for u8 {
    const VALUES: [u16; 2] = [5, 7];
}

impl Seed<3> for u16 {
    const VALUES: [u16; 3] = [11, 13, 17];
}

struct Packet<T: Seed<N>, const N: usize> {
    values: [u16; N] = T::VALUES,
    marker: PhantomData<T> = PhantomData,
}

fn main() {
    let short: Packet<u8, 2> = Packet { .. };
    let long: Packet<u16, 3> = Packet { .. };
    assert_eq!(short.values, [5, 7]);
    assert_eq!(long.values, [11, 13, 17]);
}
