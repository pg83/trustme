//@ proc-macro-aux-build: cov99_w321_impl_identity.rs
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

extern crate cov99_w321_impl_identity as impl_attr;

pub struct Probe;

macro_rules! associated_value {
    () => { pub const VALUE: u32 = 321; };
}

#[impl_attr::keep]
impl Probe {
    associated_value!();
}

pub const OBSERVED: u32 = Probe::VALUE;
