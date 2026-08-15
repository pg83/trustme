#![feature(type_alias_impl_trait)]

//@ compile-flags: -Znext-solver

struct Record {
    value: u32,
}

fn main() {
    type Hidden = impl Sized;
    let record: Hidden = Record { value: 1 };
    assert_eq!(record.value, 1);
}
