#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub struct Packet<const N: usize>
where
    [(); N + 1]:,
{
    pub bytes: [u8; N + 1],
}
