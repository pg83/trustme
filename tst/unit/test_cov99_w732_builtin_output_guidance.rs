//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(unboxed_closures, fn_traits)]

trait Select {
    fn select() -> Self;
}

impl<T: ?Sized> Select for &T {
    fn select() -> Self {
        panic!("unused branch")
    }
}

fn call<F: FnOnce<()>>(f: F) -> <F as FnOnce<()>>::Output {
    f()
}

fn main() {
    let function = || 37u8;
    let value = if false { Select::select() } else { &call(function) };
    let _ = value;
}
