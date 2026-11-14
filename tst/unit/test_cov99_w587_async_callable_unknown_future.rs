//@ compile-fail: type annotations needed
//@ compile-flags: --emit=metadata

fn make<T>() -> T {
    loop {}
}

fn demand<F: std::ops::AsyncFn()>(_: F) {}

fn main() {
    demand(make);
}
