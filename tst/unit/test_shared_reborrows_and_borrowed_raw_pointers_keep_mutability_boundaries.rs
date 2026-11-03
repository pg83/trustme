//@ reject-flags: --edition=2021 --cfg bad_raw => Type mismatch || mismatched types
//@ reject-flags: --edition=2021 --cfg bad_reference => Type mismatch || mismatched types
//@ reject-flags: --edition=2021 --cfg bad_value => Type mismatch || mismatched types
use std::ops::{Deref, DerefMut};
struct Wrap<T>(T);
impl<T> Deref for Wrap<T> { type Target = T; fn deref(&self) -> &T { &self.0 } }
impl<T> DerefMut for Wrap<T> { fn deref_mut(&mut self) -> &mut T { &mut self.0 } }
fn read(value: &u32) -> u32 { *value }
#[cfg(bad_raw)]
fn invalid() { let value = 17u32; let _: *mut u32 = &value; }
#[cfg(bad_reference)]
fn invalid() { let value = 17u32; let _: &mut u32 = &value; }
#[cfg(bad_value)]
fn invalid() { let value = 17u32; let _: u64 = &value; }
fn main() {
    let mut value = 23u32;
    assert_eq!(read(&mut value), 23);
    let pointer: *const u32 = &value;
    assert_eq!(unsafe { *pointer }, 23);
    let writable: *mut u32 = &mut value;
    unsafe { *writable = 29; }
    let mut wrapped = Wrap(31u32);
    assert_eq!(read(&mut wrapped), 31);
    let mut boxed = Box::new(Wrap(37u32));
    assert_eq!(read(&mut boxed), 37);
    assert_eq!(value, 29);
}
