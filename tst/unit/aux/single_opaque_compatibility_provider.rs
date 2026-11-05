#![feature(type_alias_impl_trait)]

pub type Hidden = impl Copy;

#[define_opaque(Hidden)]
pub fn make() -> Hidden { 79u32 }
