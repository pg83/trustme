//@ compile-fail: Type mismatch
trait Project { type Output; }
struct Identity<const N: usize>;
impl<const N: usize> Project for Identity<N> { type Output = [u16; N]; }
fn build<const N: usize>() -> <Identity<N> as Project>::Output { [859; N] }
fn main() { let _: [u16; 2] = build::<3>(); }
