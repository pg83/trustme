//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --emit=metadata

#![feature(no_core, lang_items, with_negative_coherence)]
#![no_core]
#[lang = "pointee_sized"]
trait PointeeSized {}
#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}
#[lang = "sized"]
trait Sized: MetaSized {}

#[lang = "fn_ptr_trait"]
trait Callable {}
trait Render {}
// References cannot be function pointers, so these impls are disjoint.
impl<T: Render> Render for &T {}
impl<F: Callable> Render for F {}
