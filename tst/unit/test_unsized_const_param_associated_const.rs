#![feature(adt_const_params, generic_const_exprs, unsized_const_params)]
#![allow(dead_code, incomplete_features)]

const fn append_zero<const N: usize>(_: &[u8; N]) -> [u8; N + 1]
where
    [(); N + 1]:,
{
    unimplemented!()
}

struct Bytes<const VALUE: &'static [u8]>;

impl<const VALUE: &'static [u8]> Bytes<VALUE>
where
    [(); VALUE.len() + 1]:,
{
    const ZEROS: &'static [u8; VALUE.len()] = &[0; VALUE.len()];
    const EXTENDED: &'static [u8] = &append_zero(Self::ZEROS);
}

fn main() {}
