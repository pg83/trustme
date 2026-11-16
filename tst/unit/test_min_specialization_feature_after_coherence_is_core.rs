//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --emit=metadata

#![feature(no_core, lang_items, rustc_attrs)]
#![no_core]
// Later feature attributes must take effect after rustc_coherence_is_core.
#![rustc_coherence_is_core]
#![feature(min_specialization)]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

pub enum Option<T> {
    Some(T),
}

#[rustc_specialization_trait]
pub trait TrustedStep {}

pub struct Range<A>(A);

pub trait RangeIteratorImpl {
    type Item;
    fn spec_next() -> Option<Self::Item>;
}

impl<A> RangeIteratorImpl for Range<A> {
    type Item = A;
    default fn spec_next() -> Option<A> {
        loop {}
    }
}

impl<T: TrustedStep> RangeIteratorImpl for Range<T> {
    fn spec_next() -> Option<T> {
        loop {}
    }
}
