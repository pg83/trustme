//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(type_alias_impl_trait)]

pub type Hidden = impl Sized;

#[define_opaque(Hidden)]
pub fn make_hidden() -> Hidden {
    17u8
}

fn needs_send<T: Send>(_: T) {}

pub fn send_hidden(value: Hidden) {
    needs_send(value);
}
