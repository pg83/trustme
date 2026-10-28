#![feature(transmutability)]

use std::mem::{Assume, TransmuteFrom};

fn check<S, D>()
where
    D: TransmuteFrom<S, { Assume { alignment: false, lifetimes: false, safety: true, validity: false } }>,
{}

fn assumed<S, D>()
where
    D: TransmuteFrom<S, { Assume { alignment: false, lifetimes: false, safety: true, validity: true } }>,
{}

fn main() {
    check::<bool, u8>();
    check::<u16, [u8; 2]>();
    check::<[u8; 2], u16>();
    check::<&'static u8, &'static u8>();
    check::<&'static mut u8, &'static u8>();
    assumed::<u8, bool>();
    assert_eq!(unsafe { std::mem::transmute::<bool, u8>(true) }, 1);
    assert_eq!(unsafe { std::mem::transmute::<[u8; 2], u16>([0x34, 0x12]) }, 0x1234);
}
