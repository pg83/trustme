#![feature(unsize)]
use std::marker::Unsize;
trait Value { fn get(&self) -> u32; fn set(&mut self, value: u32); }
trait Detail: Value {}
impl Value for u32 { fn get(&self) -> u32 { *self } fn set(&mut self, value: u32) { *self = value; } }
impl Detail for u32 {}
fn erase<T: ?Sized + Unsize<dyn Value>>(source: *const T) -> *const dyn Value { source as *const dyn Value }
fn main() {
    let mut value = 37u32;
    let source = &mut value as *mut u32;
    let mutable = source as *mut dyn Value;
    unsafe { (*mutable).set(41); }
    let shared = source as *const dyn Value;
    assert_eq!(unsafe { (*shared).get() }, 41);
    let inferred = erase(source as *const u32);
    assert_eq!(unsafe { (*inferred).get() }, 41);
    let detailed = source as *const dyn Detail;
    let upcast = erase(detailed);
    assert_eq!(unsafe { (*upcast).get() }, 41);
    assert_eq!(value, 41);
}
