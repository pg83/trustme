//@ compile-fail: type annotations needed
trait View<'a> { type Output; fn read(self) -> Self::Output; }
struct Record<'a>(&'a u16);
impl<'a> View<'a> for Record<'a> {
    type Output = &'a u16;
    fn read(self) -> &'a u16 { self.0 }
}
trait SliceView<'a> { type Output; fn first(self) -> Self::Output; }
impl<'a> SliceView<'a> for &'a [u16] {
    type Output = &'a u16;
    fn first(self) -> &'a u16 { &self[0] }
}
fn main() {
    let values = [37u16, 91];
    assert_eq!(*Record(&values[1]).read(), 91);
    assert_eq!(*(&values[..]).first(), 37);
}
