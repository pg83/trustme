//@ crate-type: lib
//@ compile-fail: Failed to find an impl of
//@ compile-only
//@ compile-flags: -Z stop-after=typeck
#![feature(no_core, lang_items)]
#![no_core]
#![allow(dead_code, unreachable_code, unused_variables, unused_assignments, type_alias_bounds)]

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
fn choose<T>() -> T where Selector: Select<Output = T> { loop {} }

pub trait Missing {}
fn finish<T: Missing>() {}

pub fn probe() {
    let (output, left, mut selector): Linked<_, _> = loop {};
    selector = choose();
    let _ = [output, left + 1i32];
    finish::<Selector>();
}
