//@ proc-macro-aux-build: cov99_w306_specialisable_receiver.rs
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(specialization)]
#![allow(incomplete_features, dead_code)]

trait Probe {
    fn probe();
}

#[cov99_w306_specialisable_receiver::check]
impl Probe for () {
    default fn probe() {}
}

fn main() {}
