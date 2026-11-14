//@ compile-only
//@ compile-flags: --emit=metadata

trait Pick<Arg = Self> {
    fn pick(value: Arg) -> Self;
}

impl<T> Pick<u8> for T {
    fn pick(_: u8) -> Self {
        loop {}
    }
}

fn unknown<T>() -> T {
    loop {}
}

fn main() {
    let _: u8 = <_ as Pick>::pick(unknown());
}
