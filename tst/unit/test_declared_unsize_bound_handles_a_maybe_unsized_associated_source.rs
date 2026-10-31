#![feature(unsize)]
use std::marker::Unsize;
trait Base { fn value(&self) -> u32; }
trait Detail: Base {}
impl Base for u32 { fn value(&self) -> u32 { *self } }
impl Detail for u32 {}
trait Family { type Item: ?Sized + Base; }
struct Scalar;
struct Dynamic;
impl Family for Scalar { type Item = u32; }
impl Family for Dynamic { type Item = dyn Detail; }
fn erase<T: Family>(value: &T::Item) -> &dyn Base where T::Item: Unsize<dyn Base> { value }
fn main() {
    let value = 37u32;
    assert_eq!(erase::<Scalar>(&value).value(), 37);
    let object: &dyn Detail = &value;
    assert_eq!(erase::<Dynamic>(object).value(), 37);
}
