//@ crate-type: lib
//@ compile-fail: Failed to locate item for

#![feature(type_alias_impl_trait)]

struct HiddenValue;
type MeasuredRoot = impl Sized;

#[define_opaque(MeasuredRoot)]
fn make_root() -> MeasuredRoot {
    HiddenValue
}

impl MeasuredRoot {
    const COUNT: usize = 9;
}

pub struct CountDemand([u8; <MeasuredRoot>::COUNT]);
