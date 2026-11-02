//@ compile-fail: cannot be evaluated at compile-time
#![feature(const_drop_in_place, const_trait_impl, const_destruct)]
use std::mem::ManuallyDrop;
struct RuntimeOnly;
impl Drop for RuntimeOnly { fn drop(&mut self) {} }
const CHECK: () = {
    let mut values = ManuallyDrop::new([RuntimeOnly]);
    unsafe { std::ptr::drop_in_place((&raw mut values as *mut [RuntimeOnly; 1]) as *mut [RuntimeOnly]); }
};
fn main() { let _ = CHECK; }
