#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
fn admitted<S, D>() where D: TransmuteFrom<S, { Assume { alignment: false, lifetimes: false, safety: true, validity: false } }> {}
fn main() {
    admitted::<char, u32>();
    admitted::<[char; 2], [u32; 2]>();
    admitted::<&'static char, &'static u32>();
    let edges = ['\0', '\u{d7ff}', '\u{e000}', '\u{10ffff}'];
    let bits: [u32; 4] = unsafe { std::mem::transmute(edges) };
    assert_eq!(bits, [0, 0xd7ff, 0xe000, 0x10ffff]);
}
