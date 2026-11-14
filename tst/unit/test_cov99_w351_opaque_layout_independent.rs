//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(type_alias_impl_trait)]

type Captured = impl Copy;

#[define_opaque(Captured)]
const fn captured() -> Captured {
    let byte = 3u8;
    move || byte
}

const fn width() -> usize {
    core::mem::size_of::<Captured>()
}

const WIDTH: usize = width();
pub const CHECK: [u8; WIDTH] = [0; WIDTH];
