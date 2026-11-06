#![feature(specialization, pattern_types, pattern_type_macro, generic_pattern_types)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

type Closed = std::pat::pattern_type!(usize is 0..=3);
type Exclusive = std::pat::pattern_type!(usize is 0..3);
type Split = std::pat::pattern_type!(usize is 0..=1 | 3..=4);
type OtherBase = std::pat::pattern_type!(u16 is 0..=3);
type Longer = std::pat::pattern_type!(usize is 0..=7);

trait Domain {
    type Value;
}

struct Source;
impl Domain for Source {
    type Value = Closed;
}

struct Marker<T>(PhantomData<T>);

trait Same<Rhs> {
    fn check() -> bool;
}

impl<T, Rhs> Same<Rhs> for Marker<T> {
    default fn check() -> bool { false }
}

impl<T: Domain> Same<T::Value> for Marker<T> {
    fn check() -> bool { true }
}

fn main() {
    assert!(<Marker<Source> as Same<Closed>>::check());
    assert!(!<Marker<Source> as Same<OtherBase>>::check());
    assert!(!<Marker<Source> as Same<Split>>::check());
    assert!(!<Marker<Source> as Same<Exclusive>>::check());
    assert!(!<Marker<Source> as Same<Longer>>::check());
}
