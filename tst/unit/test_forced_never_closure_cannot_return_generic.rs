//@ compile-fail: /*M:0*/ and !
//@ crate-type: lib
#![feature(never_type)]

fn require_never<F: FnOnce() -> !>(_: &F) {}

fn unknown<T, F: FnOnce() -> T>() -> F {
    panic!("not called")
}

pub fn check<T>() {
    let callback = || panic!("not called");
    require_never(&callback);
    let mut other = unknown::<T, _>();
    other = callback;
    drop(other);
}
