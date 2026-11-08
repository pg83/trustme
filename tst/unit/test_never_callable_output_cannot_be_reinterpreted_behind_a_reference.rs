//@ compile-fail: Type mismatch between
#![feature(unboxed_closures)]

fn never_returns() -> ! {
    loop {}
}

fn borrowed_output<F: FnOnce<()>>(_: F) -> &'static F::Output {
    loop {}
}

fn invalid_view<T: 'static>() {
    let _: &'static Option<T> = borrowed_output(never_returns);
}

fn main() {}
