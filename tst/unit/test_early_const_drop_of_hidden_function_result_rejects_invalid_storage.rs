//@ compile-flags: -Zunleash-the-miri-inside-of-you
//@ compile-fail: error
#![feature(const_drop_in_place, const_trait_impl, const_destruct)]

struct Resource(u8);
impl Drop for Resource { fn drop(&mut self) {} }

const fn a_factory() -> impl FnOnce() {
    let owned = Resource(113);
    move || drop(owned)
}

const fn drop_return<F: FnOnce() -> R, R>(_: F) {
    let mut empty = ();
    unsafe { std::ptr::drop_in_place((&raw mut empty).cast::<R>()); }
}

pub fn z_target() -> [u8; { drop_return(a_factory); 0 }] { [] }

fn main() { let _ = z_target(); }
