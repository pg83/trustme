//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
trait PointeeSized {}
#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}
#[lang = "sized"]
trait Sized: MetaSized {}

struct Option<T>(T);
trait From<T> {}

impl<T> From<T> for Option<T> {}
impl<T> From<&Option<T>> for Option<&T> {}
