//@ compile-only
//@ compile-flags: --emit=metadata

fn make<T>() -> T {
    loop {}
}

fn main() {
    let (element,) = make();
    let mut pointer = &raw const element;
    pointer = &37u8;
}
