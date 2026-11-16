//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(lang_items, no_core)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "copy"]
pub trait Copy {}

#[lang = "div"]
pub trait Div<Rhs = Self> {
    type Output;
    fn div(self, rhs: Rhs) -> Self::Output;
}

impl Div for usize {
    type Output = usize;
    fn div(self, _rhs: usize) -> usize { 2 }
}

pub struct Buffer(pub [(); 16 / 8usize]);

impl Buffer {}
