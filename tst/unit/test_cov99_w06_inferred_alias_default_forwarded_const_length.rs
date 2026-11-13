#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

type Buffer<T, const N: usize = { core::mem::size_of::<T>() }> = [T; N];

const fn repeat_length<T, const N: usize>(_: &[T; N]) -> usize {
    let repeated = [0u8; N];
    repeated.len()
}

const fn inferred_width<T: Copy>(value: T) -> usize
where
    [(); core::mem::size_of::<T>()]:,
{
    let buffer: Buffer<_> = [value; core::mem::size_of::<T>()];
    repeat_length(&buffer)
}

const BYTE: usize = inferred_width(3u8);
const WORD: usize = inferred_width(7u32);

fn main() {
    assert_eq!(BYTE, 1);
    assert_eq!(WORD, 4);
}
