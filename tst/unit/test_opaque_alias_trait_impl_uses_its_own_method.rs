#![feature(type_alias_impl_trait)]
type Hidden = impl Copy;
trait Code { fn code(self) -> u8; }
impl Code for Hidden { fn code(self) -> u8 { 23 } }
#[define_opaque(Hidden)]
fn make() -> Hidden { 7u8 }
fn main() { assert_eq!(make().code(), 23); }
