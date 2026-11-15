#![no_std]

pub struct AliasType<const N: usize>(pub [u8; N]);
pub struct AliasTrait<const N: usize>(pub [u8; N]);
