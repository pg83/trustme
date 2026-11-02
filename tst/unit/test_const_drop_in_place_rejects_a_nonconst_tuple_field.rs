//@ compile-fail: cannot be evaluated at compile-time
#![feature(const_drop_in_place, const_trait_impl, const_destruct)]
use std::mem::ManuallyDrop;
struct RuntimeOnly;
impl Drop for RuntimeOnly { fn drop(&mut self) {} }
const CHECK: () = {
    let mut value = ManuallyDrop::new((17u32, RuntimeOnly));
    unsafe { std::ptr::drop_in_place(&raw mut value as *mut (u32, RuntimeOnly)); }
};
fn main() { let _ = CHECK; }
