#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use std::ops::Index;

enum State<const N: usize> {
    Empty([(); N]),
}

struct Table<const N: usize>;

impl<const N: usize> Table<N>
where
    [State<N>; N * N]: Sized,
{
    fn read() {
        let State::Empty(_) = Self[()];
    }
}

impl<const N: usize> Index<()> for Table<N>
where
    [State<N>; N * N]: Sized,
{
    type Output = State<N>;

    fn index(&self, _: ()) -> &Self::Output {
        loop {}
    }
}

fn main() {}
