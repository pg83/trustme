// A generic constant borrowed in a generic function is promoted per
// instance, as upstream promotes `&Owned::<T>::VTABLE` to a static of each
// monomorphised value. It used to be left a stack temporary, so the
// `&'static` reference dangled once the frame was reused: bytes' vtables.
use std::marker::PhantomData;
struct Vtable {
    size: fn() -> usize,
    name: fn() -> &'static str,
}
struct Owned<T>(PhantomData<T>);
impl<T> Owned<T> {
    const VTABLE: Vtable = Vtable { size: std::mem::size_of::<T>, name: std::any::type_name::<T> };
}
struct Handle {
    vtable: &'static Vtable,
}
#[inline(never)]
fn handle<T>() -> Handle {
    Handle { vtable: &Owned::<T>::VTABLE }
}
#[inline(never)]
fn churn(n: u32) -> u32 {
    let pad = [n; 64];
    std::hint::black_box(&pad);
    if n == 0 { 0 } else { churn(n - 1) + 1 }
}
fn main() {
    let h = handle::<u64>();
    churn(16);
    assert_eq!((h.vtable.size)(), 8);
    assert_eq!((h.vtable.name)(), "u64");
    assert!(std::ptr::eq(h.vtable, handle::<u64>().vtable));
}
