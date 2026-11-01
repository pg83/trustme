#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Width { const N: usize; }
struct Narrow;
struct Wide;
impl Width for Narrow { const N: usize = 3; }
impl Width for Wide { const N: usize = 5; }

fn read<T: Width>(value: &[u16; T::N], index: usize) -> u16
where [(); T::N]: {
    value[index]
}

fn main() {
    assert_eq!(read::<Narrow>(&[3, 5, 7], 2), 7);
    assert_eq!(read::<Wide>(&[11, 13, 17, 19, 23], 4), 23);
}
