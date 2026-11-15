//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(inherent_associated_types)]

struct Shell<T>(T);

impl<T> Shell<(T,)> {
    type Item = T;
}

pub fn probe() {
    let _: *const Shell<_>::Item = 0usize as *const u8;
    let pending = || ();
    pending();
}
