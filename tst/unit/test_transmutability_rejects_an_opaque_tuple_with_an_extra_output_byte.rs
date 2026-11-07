//@ compile-fail: error:
#![feature(type_alias_impl_trait, transmutability)]

use std::mem::{Assume, TransmuteFrom};

type Hidden = impl Copy;

#[define_opaque(Hidden)]
const SOURCE: Hidden = 101u16;

fn require<S, D>()
where
    D: TransmuteFrom<S, { Assume::SAFETY }>,
{
}

fn main() {
    let _ = SOURCE;
    require::<(Hidden,), [u8; 3]>();
}
