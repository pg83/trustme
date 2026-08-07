#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn half_array<const N: usize>() -> [u8; N / 2]
where
    [u8; N / 2]: Sized,
{
    [7_u8; N / 2]
}

fn main() {
    let values = half_array::<4>();
    assert!(values[0] == 7);
    assert!(values[1] == 7);
}
