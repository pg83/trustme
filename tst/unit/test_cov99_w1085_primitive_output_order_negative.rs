//@ crate-type: lib
//@ compile-only
//@ compile-fail: Failed to find an impl of
//@ compile-flags: -Z stop-after=typeck
#![feature(no_core, lang_items, type_ascription)]
#![no_core]
#![allow(dead_code, unreachable_code, unused_variables, type_alias_bounds)]

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

pub struct Shape<T, U>(T, U);
pub trait Resolve { type Output; }
impl Resolve for Shape<i32, u8> { type Output = i32; }
impl Resolve for Shape<i64, u16> { type Output = i64; }
type Linked<T, U> where Shape<T, U>: Resolve =
    (T, <Shape<T, U> as Resolve>::Output, U);

pub struct Selector;
pub trait Select { type Output; }
impl Select for Selector { type Output = u8; }
fn consume<T, R, U>(_: (T, R, U)) where Selector: Select<Output = U> {}
pub trait Missing {}
fn finish<T: Missing>() {}

pub fn probe() {
    consume(((1i32 + 2i32): Linked<_, _>));
    finish::<Selector>();
}
