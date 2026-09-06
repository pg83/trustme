//@ run-pass
// `NonNull::dangling().as_ptr() as _` with `*const T` asked for: upstream
// checks a cast as a coercion first (`try_coercion_cast`), and the
// `*mut ?U -> *const T` coercion is `coerce_raw_ptr` unifying the pointees
// once `?U: Unsize<T>` proved ambiguous - so `?U` is `T`, and `dangling`'s
// `Sized` bound is on a known type.
use std::ptr;

const fn dangling<T>() -> *const T {
    ptr::NonNull::dangling().as_ptr() as _
}

fn main() {
    let p: *const u64 = dangling();
    assert_eq!(p as usize, std::mem::align_of::<u64>());
}
