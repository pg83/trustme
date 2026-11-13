#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Factory;

impl Factory {
    fn item() -> u8 {
        7
    }
}

trait Length {
    fn length(self) -> usize;
}

impl<const N: usize> Length for [u8; N] {
    fn length(self) -> usize {
        N
    }
}

fn bytes<F>(_: F) -> [u8; core::mem::size_of::<F>()]
where
    [u8; core::mem::size_of::<F>()]:,
{
    [0; core::mem::size_of::<F>()]
}

fn main() {
    assert_eq!(Length::length(bytes(Factory::item)), 0);
}
