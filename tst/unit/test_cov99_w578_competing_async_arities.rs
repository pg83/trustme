//@ compile-only
//@ compile-flags: --emit=metadata

trait ZeroArity: Sized {
    fn selected(self) -> u8 {
        0
    }
}

impl<F: std::ops::AsyncFnOnce() -> u8> ZeroArity for F {}

trait OneArity: Sized {
    fn selected(self) -> u8 {
        1
    }
}

impl<F: std::ops::AsyncFnOnce(u8) -> u8> OneArity for F {}

async fn one(value: u8) -> u8 {
    value
}

fn main() {
    assert_eq!(one.selected(), 1);
}
