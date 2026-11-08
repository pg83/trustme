trait Leaf<const N: usize> {}
impl Leaf<3> for u8 {}
trait Make { fn make() -> Self; }
impl<const N: usize> Make for [u8; N] where u8: Leaf<N> {
    fn make() -> Self { [7; N] }
}
fn main() {
    let data = <[u8; _] as Make>::make();
    assert_eq!(data.len(), 3);
    assert_eq!(data[2], 7);
}
