#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn count(n: usize) -> usize { n + 1 }

fn read<const N: usize>(value: [u8; count(N)]) -> u8
where [(); count(N)]: { value[0] }

fn widen<const N: usize>(value: fn([u8; count(N)]) -> u8)
    -> unsafe fn([u8; count(N)]) -> u8
where [(); count(N)]: { value }

fn main() {
    let value = widen::<3>(read::<3>);
    assert_eq!(unsafe { value([97u8; 4]) }, 97);
}
