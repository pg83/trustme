//@ compile-fail: type annotations needed
//@ compile-flags: --emit=metadata -C panic=abort

trait Source {
    type Output: ?Sized + 'static;
}

impl Source for u8 {
    type Output = u8;
}

impl Source for u16 {
    type Output = u16;
}

trait Destination {}
impl Destination for u8 {}
impl Destination for u16 {}

fn source<T: Source>() -> &'static T::Output {
    loop {}
}

fn destination<T: Destination>() -> &'static T {
    loop {}
}

fn main() {
    let mut value = destination();
    value = source::<_>();
}
