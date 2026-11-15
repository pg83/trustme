//@ compile-fail: TransmuteFrom
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(transmutability)]

use core::mem::{Assume, TransmuteFrom};

fn require_transmute<Src, Dst>()
where
    Dst: TransmuteFrom<Src, { Assume::NOTHING }>,
{
}

fn main() {
    require_transmute::<&'static [u8; 2], &'static u16>();
}
