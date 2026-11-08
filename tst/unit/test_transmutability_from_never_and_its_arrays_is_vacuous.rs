#![feature(never_type, transmutability)]

use std::mem::{Assume, TransmuteFrom};

fn require<S, D: TransmuteFrom<S, { Assume::SAFETY }>>() {}

fn main() {
    require::<! , ()>();
    require::<! , bool>();
    require::<[!; 1], u32>();
    require::<(!, u8), u64>();
    require::<[!; 0], ()>();
    let empty: [!; 0] = [];
    let unit = unsafe { <() as TransmuteFrom<[!; 0], { Assume::SAFETY }>>::transmute(empty) };
    assert_eq!(unit, ());
}
