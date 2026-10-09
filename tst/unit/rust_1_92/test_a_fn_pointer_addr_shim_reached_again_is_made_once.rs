// Comparing fn pointers goes through core's `impl<F: FnPtr> PartialEq for F`
// and the builtin `FnPtr::addr` shim of the pointer type. The pointers here
// come out of inline consts evaluated during monomorphisation, and walking
// what their values point to reaches `same` and its shim again. rustc's mono
// item collector keeps one visited set for the whole walk, constants'
// allocations included, so each is collected once. divan's
// `EntryConst::cmp_name` is reached this way, and the compiler crashed making
// the shim a second time.
use std::cmp::Ordering;

type Compare = unsafe fn(*const (), *const ()) -> Option<Ordering>;
type Check = fn(Compare, Compare) -> bool;

unsafe fn never(_: *const (), _: *const ()) -> Option<Ordering> {
    None
}

fn same(a: Compare, b: Compare) -> bool {
    a == b
}

fn same_for<T>(a: Compare, b: Compare) -> bool {
    same(a, b)
}

const fn pick<T>() -> Check {
    same_for::<T>
}

fn inner<T>() -> &'static Check {
    const { &pick::<T>() }
}

const fn pick_inner<T>() -> fn() -> &'static Check {
    inner::<T>
}

fn outer<T>() -> &'static fn() -> &'static Check {
    const { &pick_inner::<T>() }
}

fn main() {
    assert!(same(never, never));
    assert!(outer::<u8>()()(never, never));
}
