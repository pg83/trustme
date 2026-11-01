//@ compile-variant: -O
#![feature(core_intrinsics)]
use std::marker::PhantomData;
use std::mem::ManuallyDrop;
struct Guard<T>(PhantomData<T>);
impl<T> Drop for Guard<T> { fn drop(&mut self) {} }
#[inline(never)]
fn needs<T>() -> bool { const { std::intrinsics::needs_drop::<T>() } }
fn main() {
    assert!(!needs::<()>());
    assert!(!needs::<[String; 0]>());
    assert!(needs::<[String; 1]>());
    assert!(needs::<Option<Box<u32>>>());
    assert!(!needs::<ManuallyDrop<String>>());
    assert!(needs::<Guard<()>>());
    assert!(!const { std::intrinsics::needs_drop::<[Guard<u8>; 0]>() });
}
