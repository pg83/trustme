#![feature(type_alias_impl_trait)]
use std::ops::Deref;

type Handle = impl Deref<Target = u32>;

fn read(value: &u32) -> u32 { *value }

#[define_opaque(Handle)]
fn make() -> Handle {
    let value: Handle = Box::new(67u32);
    assert_eq!(read(&value), 67);
    value
}

fn main() {
    assert_eq!(read(&make()), 67);
}
