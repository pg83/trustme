//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Seen {}
impl<F, G> Seen for (F, G) {}

fn record<F, G, A, B>(_: &(F, G))
where
    F: Fn(A) -> A,
    G: Fn(B) -> B,
    (F, G): Seen,
{}

pub fn exercise() -> (u8, u16) {
    let pair = (|x| x, |y| y);
    record(&pair);
    let first = (pair.0)(7u8);
    record(&pair);
    let second = (pair.1)(9u16);
    record(&pair);
    (first, second)
}
