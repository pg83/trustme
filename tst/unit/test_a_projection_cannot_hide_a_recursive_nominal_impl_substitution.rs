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

trait Project { type Output; }
struct Pair<A, B>(A, B);
trait Relation<T> {}

impl<T> Relation<T> for T {}
impl<T: Project> Relation<Pair<T::Output, T>> for T {}
