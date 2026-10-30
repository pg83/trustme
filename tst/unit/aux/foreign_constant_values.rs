#![feature(type_alias_impl_trait)]
pub type Operation=impl Fn(u16)->u16;
#[define_opaque(Operation)]
pub const OPERATION:Operation={let offset=11u16;move |value|value+offset};
pub type Values=impl AsRef<[u16]>+Copy;
#[define_opaque(Values)]
pub const VALUES:Values=[3u16,5,7];
