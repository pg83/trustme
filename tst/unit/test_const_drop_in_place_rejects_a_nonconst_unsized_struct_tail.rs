//@ compile-fail: cannot be evaluated at compile-time
#![feature(const_drop_in_place, const_trait_impl, const_destruct)]
use std::mem::ManuallyDrop;
struct RuntimeOnly;
impl Drop for RuntimeOnly { fn drop(&mut self) {} }
struct Batch<T: ?Sized> { tag: u8, tail: T }
const CHECK: () = {
    let mut value = ManuallyDrop::new(Batch { tag: 7, tail: [RuntimeOnly] });
    let sized = &raw mut value as *mut Batch<[RuntimeOnly; 1]>;
    unsafe { std::ptr::drop_in_place(sized as *mut Batch<[RuntimeOnly]>); }
};
fn main() { let _ = CHECK; }
