//@ compile-fail: type annotations needed
//@ compile-flags: --emit=metadata -Cpanic=abort

fn make<T>() -> T {
    loop {}
}

fn main() {
    let _: () = match *make() {};
}
