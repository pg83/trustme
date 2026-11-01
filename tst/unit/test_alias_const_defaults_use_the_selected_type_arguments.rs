#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

type Buffer<T = u16, const N: usize = { std::mem::size_of::<T>() }> = (T, [u8; N]);

fn main() {
    let first: Buffer = (3, [5, 7]);
    let second: Buffer<u32> = (11, [13, 17, 19, 23]);
    let third: Buffer<u16, 1> = (29, [31]);
    assert_eq!(first.0, 3);
    assert_eq!(first.1[1], 7);
    assert_eq!(second.1[3], 23);
    assert_eq!(third.1[0], 31);
}
