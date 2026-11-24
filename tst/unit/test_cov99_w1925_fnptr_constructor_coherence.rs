//@ crate-type: lib
//@ edition: 2024
//@ compile-flags: --emit=metadata

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

#[cfg_attr(not(rust_compiler = "trustme"), rustc_builtin_macro)]
macro_rules! define_opaque { ($($token:tt)*) => {}; }

#[lang = "fn_ptr_trait"]
trait Callable {}

struct Hub;
struct Nominal;
trait Object {}
type Hidden = impl Sized;

#[define_opaque(Hidden)]
fn hidden() -> Hidden {}

trait Route<A, B: ?Sized> {}
impl<F: Callable> Route<F, u16> for Hub {}
impl<F: Callable> Route<F, Nominal> for Hub {}
impl<F: Callable> Route<F, [u8; 3]> for Hub {}
impl<F: Callable> Route<F, [u8]> for Hub {}
impl<F: Callable> Route<F, (u8, u16)> for Hub {}
impl<F: Callable> Route<F, &u8> for Hub {}
impl<F: Callable> Route<F, *const u8> for Hub {}
impl<F: Callable> Route<F, dyn Object> for Hub {}
impl<T: ?Sized + Callable> Route<Hidden, T> for Hub {}
