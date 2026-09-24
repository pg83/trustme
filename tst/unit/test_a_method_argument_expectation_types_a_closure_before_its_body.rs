// hashbrown's `reserve_rehash` passes
// `if T::NEEDS_DROP { Some(|ptr| ptr::drop_in_place(ptr.cast::<T>())) } else { None }`
// to a method taking `Option<unsafe fn(*mut u8)>`. rustc checks a method
// argument with the expected input already known, so the closure's parameter
// is `*mut u8` before its body's `ptr.cast()` is looked up. Our coercion of the
// argument waited for that lookup - a binding inside its own argument - so the
// variant's parameter fell back to the closure type and the coercion failed.
use std::ptr;

struct Table;

impl Table {
    unsafe fn rehash<A>(&mut self, _alloc: &A, drop: Option<unsafe fn(*mut u8)>, p: *mut u8) {
        if let Some(drop) = drop {
            drop(p);
        }
    }
}

fn clear<T>(value: T) -> bool {
    let mut value = std::mem::ManuallyDrop::new(value);
    let p = &mut *value as *mut T as *mut u8;
    let needs_drop = std::mem::needs_drop::<T>();
    let mut table = Table;
    unsafe {
        table.rehash(
            &(),
            if needs_drop {
                Some(|ptr| ptr::drop_in_place(ptr.cast::<T>()))
            } else {
                None
            },
            p,
        );
    }
    needs_drop
}

fn main() {
    assert!(clear(String::from("dropped once")));
    assert!(!clear(7u32));
}
