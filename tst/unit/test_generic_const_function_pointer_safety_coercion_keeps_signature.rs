#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn count(n: usize) -> usize { n + 1 }
struct Buffer<const N: usize>([u8; N]);

fn read<const N: usize>(value: Buffer<{ count(N) }>) -> u8
where [(); count(N)]: { value.0[0] }

fn widen<const N: usize>(value: fn(Buffer<{ count(N) }>) -> u8)
    -> unsafe fn(Buffer<{ count(N) }>) -> u8
where [(); count(N)]: { value }

fn main() {
    let value = widen::<2>(read::<2>);
    assert_eq!(unsafe { value(Buffer([89u8; 3])) }, 89);
}
