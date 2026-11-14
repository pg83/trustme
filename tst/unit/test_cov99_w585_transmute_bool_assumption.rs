//@ compile-fail: ThinTransmute
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "copy"]
pub trait Copy {}
impl Copy for bool {}

#[lang = "transmute_trait"]
pub unsafe trait ThinTransmute<Source, const ASSUME: bool> {}

fn require<D: ThinTransmute<u8, true>>() {}

fn main() {
    require::<u8>();
}
