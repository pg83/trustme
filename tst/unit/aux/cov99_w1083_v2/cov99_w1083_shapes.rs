#![no_std]
#![feature(trait_alias)]

pub type AliasType<const N: usize> = [u8; N];
pub trait AliasTrait<const N: usize> = Copy;
