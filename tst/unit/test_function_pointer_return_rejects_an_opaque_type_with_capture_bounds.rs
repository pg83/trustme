//@ compile-fail: error
#![feature(type_alias_impl_trait)]
#![allow(dead_code)]
trait Bound<'a> {}
type Callback<'a> = fn() -> impl ?Sized + Bound<'a> + 'a + use<'a>;
fn main() {}
