//@ aux-build: own_projected_rpit.rs
#![feature(return_type_notation)]

use own_projected_rpit::{Package, Source, Values};

fn sum() -> u16
where
    Values: Source<iter(..): Package<Wrapped: IntoIterator>>,
{
    Values::iter().sum()
}

fn main() {
    assert_eq!(sum(), 171);
}
