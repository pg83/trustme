//@ compile-only
//@ compile-flags: -Z stop-after=typeck -Z dump-hir

trait Select<T> {
    type Output;
}

impl<T> Select<T> for () {
    type Output = T;
}

type Twin<T, U = T> = (T, U);

fn main() {
    let pair: <() as Select<Twin<_>>>::Output = (1u8, 2u8);
    let _: (u8, u8) = pair;
}
