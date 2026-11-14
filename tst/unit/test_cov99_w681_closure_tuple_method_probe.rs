//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Tag {
    type Out;
}
impl<F, G> Tag for (F, G) {
    type Out = ();
}

trait Choose<A, B>: Tag {
    fn choose(&self, _: (fn() -> A, fn() -> (B, Self::Out))) {}
}
impl<F, G> Choose<u8, u16> for (F, G) {}
impl<F, G> Choose<u32, u64> for (F, G) {}

pub fn exercise() -> (u8, u16) {
    let pair = (|| Default::default(), || (Default::default(), ()));
    pair.choose((pair.0, pair.1));
    let first: u8 = (pair.0)();
    let second: u16 = (pair.1)().0;
    (first, second)
}
