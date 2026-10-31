#![feature(unsize)]
use std::marker::Unsize;
trait Base { fn value(&self) -> u32; }
trait Detail: Base {}
impl Base for u32 { fn value(&self) -> u32 { *self } }
impl Detail for u32 {}
fn erase<T: ?Sized + Unsize<dyn Base>>(value: &T) -> &dyn Base { value }
fn main() {
    let value = 37u32;
    assert_eq!(erase(&value).value(), 37);
    let object: &dyn Detail = &value;
    assert_eq!(erase(object).value(), 37);
}
