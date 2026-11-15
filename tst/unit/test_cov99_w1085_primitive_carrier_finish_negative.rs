//@ crate-type: lib
//@ compile-only
//@ compile-fail: error:
//@ compile-flags: -Z stop-after=typeck
#![feature(no_core, lang_items, type_ascription)]
#![no_core]
#![allow(dead_code, unused_variables, type_alias_bounds)]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "add"]
pub trait Add<Rhs = Self> {
    type Output;
    fn add(self, rhs: Rhs) -> Self::Output;
}

pub struct Shape<T>(T);
pub trait Resolve { type Output; }
impl Resolve for Shape<i32> { type Output = i32; }
type Linked<T, C> where C: Resolve = (T, <C as Resolve>::Output, C);

pub struct Selector<T>(T);
pub trait Select { type Output; }
impl<T> Select for Selector<T> { type Output = Shape<T>; }
fn consume<T, C>(arg: Linked<T, C>) -> Linked<T, C>
where Selector<T>: Select<Output = C> { arg }
pub trait Missing {}
fn finish<T: Missing>(_: &T) {}

pub fn probe() {
    let linked = consume(((1i32 + 2i32): Linked<_, _>));
    finish(&linked.2);
}
