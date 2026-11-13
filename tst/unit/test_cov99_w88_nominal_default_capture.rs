//@ edition: 2021
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Window<const N: usize, const M: usize> {
    data: [u8; N],
}

type Frame<const N: usize, const M: usize = { N + 1 }> = Window<N, M>;

impl<const N: usize, const M: usize> Window<N, M> {
    const fn width(&self) -> usize { M }
}

const WIDTH: usize = {
    let value = Frame::<_> { data: [0u8; 3] };
    value.width()
};

fn main() {
    assert_eq!(WIDTH, 4);
}
