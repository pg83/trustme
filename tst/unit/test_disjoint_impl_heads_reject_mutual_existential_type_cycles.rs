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

struct Wrap<T>(T);
struct Pair<T, U>(T, U);

trait Nominal<T, U> {}
impl<T, U> Nominal<T, U> for Pair<T, U> {}
impl<T, U> Nominal<&Wrap<U>, &Wrap<T>> for Pair<&T, &U> {}

trait Borrowed<T, U> {}
impl<T, U> Borrowed<T, U> for Pair<T, U> {}
impl<T, U> Borrowed<&&U, &&T> for Pair<&T, &U> {}
