//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(no_core, lang_items)]
#![no_core]
#![allow(internal_features)]

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for u8 {}

#[lang = "neg"]
pub trait Neg {
    type Output;
    fn neg(self) -> Self::Output;
}

impl Neg for u8 {
    type Output = u8;

    fn neg(self) -> u8 {
        self
    }
}

pub fn unsigned_negation() -> u8 {
    -1
}
