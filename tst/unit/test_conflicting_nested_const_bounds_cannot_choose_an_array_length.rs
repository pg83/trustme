//@ compile-fail: error:0:
trait First<const N: usize> {}
trait Second<const N: usize> {}
impl First<3> for u8 {}
impl Second<4> for u16 {}
trait Make { fn make() -> Self; }
impl<const N: usize> Make for [u8; N]
where u8: First<N>, u16: Second<N> {
    fn make() -> Self { [7; N] }
}
fn main() {
    let data = <[u8; _] as Make>::make();
    let _ = data.len();
}
