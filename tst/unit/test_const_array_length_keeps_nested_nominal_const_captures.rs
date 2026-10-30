#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Cell<const N: usize>([u8; N]);

const fn width<const N: usize>() -> usize
where
    [(); std::mem::size_of::<(Cell<N>, [Cell<N>; 2])>()]:,
{
    [0u8; std::mem::size_of::<(Cell<N>, [Cell<N>; 2])>()].len()
}

const FIRST: usize = width::<3>();
const SECOND: usize = width::<5>();
const EMPTY: usize = width::<0>();

fn main() {
    assert_eq!((FIRST, SECOND, EMPTY), (9, 15, 0));
}
