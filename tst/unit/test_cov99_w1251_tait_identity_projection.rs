//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(type_alias_impl_trait)]

type Local = impl Copy;

trait Identity {
    type Output;
}

impl<T> Identity for T {
    type Output = T;
}

#[define_opaque(Local)]
fn define() -> Local {
    let seed: <Local as Identity>::Output = if false { define() } else { 23u8 };
    let again: Local = seed;
    again
}
