//@ edition: 2021
#![feature(rustc_attrs)]
#![allow(internal_features)]

#[rustc_has_incoherent_inherent_impls]
pub struct Shared;

impl Shared {
    fn choose() -> u8 { 1 }
}

pub fn export() -> impl Fn() -> u8 {
    Shared::choose
}
