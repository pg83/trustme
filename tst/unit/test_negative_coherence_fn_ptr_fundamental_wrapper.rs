//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --emit=metadata

#![feature(no_core, lang_items, fundamental, with_negative_coherence)]
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
#[fundamental]
struct Wrap<T>(T);
impl<T: Render> Render for Wrap<T> {}
impl<F: Callable> Render for F {}
