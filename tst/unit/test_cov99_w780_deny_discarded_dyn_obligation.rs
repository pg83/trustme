//@ crate-type: lib
//@ compile-fail: unused implementor that must be used
//@ compile-flags: -D unused_must_use -Cpanic=abort

#[must_use]
pub trait Obligation {}

pub fn discard(value: &dyn Obligation) {
    *value;
}
