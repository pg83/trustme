//@ compile-flags: -Cpanic=abort
#![feature(type_alias_impl_trait)]

trait Read {
    fn read(&self) -> u32;
}

impl Read for u32 {
    fn read(&self) -> u32 { *self }
}

fn function_opaque<T: Read>(value: T) -> impl Read { value }

type Alias<T> = impl Read;

#[define_opaque(Alias)]
fn alias_opaque<T: Read>(value: T) -> Alias<T> { function_opaque(value) }

fn main() {
    assert_eq!(alias_opaque(7u32).read(), 7);
    assert_eq!(function_opaque(9u32).read(), 9);
}
