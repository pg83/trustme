#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Factory;

impl Factory {
    fn item() {}
}

trait Length {
    fn length(self) -> usize;
}

impl<const M: usize> Length for [u8; M] {
    fn length(self) -> usize {
        M
    }
}

fn bytes<F, const N: usize>(_: F) -> [u8; N + 1]
where
    [u8; N + 1]:,
{
    [0; N + 1]
}

fn check<const N: usize>()
where
    [u8; N + 1]:,
{
    let item = Factory::item;
    let array = bytes::<_, N>(item);
    assert_eq!(Length::length(array), N + 1);
}

fn main() {
    check::<2>();
}
