// An unsafe binder hides the lifetimes it binds. This compiler erases lifetimes,
// so the binder is its own inner type and wrapping or unwrapping a value leaves
// the value alone.
#![feature(unsafe_binders)]
#![allow(incomplete_features, dead_code, unused_unsafe)]

use std::unsafe_binder::{unwrap_binder, wrap_binder};

struct Holder {
    empty: unsafe<> (),
}

fn main() {
    unsafe {
        let value = 7;
        let bound: unsafe<'a> &'a i32 = wrap_binder!(&value);
        assert_eq!(*unwrap_binder!(bound), 7);

        let _ = Holder { empty: wrap_binder!(()) };
    }
}
