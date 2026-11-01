#![feature(trait_alias)]
use std::any::TypeId;
trait Functions = Sized where
    fn(&'static u8): Copy,
    for<'a> fn(&'a u16): Copy,
    unsafe extern "C" fn(u16) -> u32: Copy,
    unsafe extern "C" fn(u16, ...) -> u32: Copy;
fn retain<T: Functions>(value: T) -> T { value }
fn main() {
    assert_eq!(retain(53u16), 53);
    assert_ne!(TypeId::of::<fn(&'static u8)>(), TypeId::of::<for<'a> fn(&'a u8)>());
    assert_ne!(TypeId::of::<unsafe extern "C" fn(u16) -> u32>(), TypeId::of::<unsafe extern "C" fn(u16, ...) -> u32>());
    assert_eq!(std::any::type_name::<unsafe extern "C" fn(u16, ...) -> u32>(), "unsafe extern \"C\" fn(u16, ...) -> u32");
}
