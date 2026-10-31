//@ compile-fail: Type mismatch
#![feature(type_alias_impl_trait)]
type Hidden = impl Copy;
#[define_opaque(Hidden)]
const fn make() -> Hidden { 37u32 }
const fn length(_value: Hidden) -> usize { 3 }
type Selected = [u8; length(make())];
fn main() {
    let _value: Selected = [17; 3];
    let _invalid = length(37u32);
}
