//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --emit=metadata
//@ compile-fail: conflicting implementations

#![feature(no_core, lang_items, rustc_attrs, type_alias_impl_trait, with_negative_coherence)]
#![no_core]

#[lang = "pointee_sized"]
trait PointeeSized {}
#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}
#[lang = "sized"]
trait Sized: MetaSized {}
#[lang = "copy"]
trait Copy {}
#[lang = "future_trait"]
trait Future {
    type Output;
}

#[cfg_attr(not(rust_compiler = "trustme"), rustc_builtin_macro)]
macro_rules! define_opaque { ($($token:tt)*) => {}; }

#[lang = "fn_ptr_trait"]
trait Callable {}

type Hidden = impl Sized;

#[define_opaque(Hidden)]
fn make() -> Hidden {}

trait Forward {}
impl Forward for Hidden {}
impl<F: Callable> Forward for F {}

trait Reversed {}
impl<F: Callable> Reversed for F {}
impl Reversed for Hidden {}
