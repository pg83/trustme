//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

macro_rules! default_size {
    () => { 1 };
}

pub struct Marker<const N: usize = { default_size!() }>;
