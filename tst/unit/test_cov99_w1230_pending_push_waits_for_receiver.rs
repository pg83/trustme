//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: type annotations needed

fn untyped<T>() -> T {
    loop {}
}

fn main() {
    let mut receiver = untyped();
    let _: () = receiver.push(1);
}
