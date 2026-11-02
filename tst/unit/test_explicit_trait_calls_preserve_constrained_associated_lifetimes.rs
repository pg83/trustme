//@ compile-variant: -O
trait View<'a> { type Output; fn read(self) -> Self::Output; }
struct Record<'a>(&'a u32);
impl<'a> View<'a> for Record<'a> {
    type Output = &'a u32;
    fn read(self) -> &'a u32 { self.0 }
}
trait SliceView<'a> { type Output; fn first(self) -> Self::Output; }
impl<'a> SliceView<'a> for &'a [u32] {
    type Output = &'a u32;
    fn first(self) -> &'a u32 { &self[0] }
}
fn main() {
    let values = [41u32, 67];
    assert_eq!(*View::read(Record(&values[1])), 67);
    assert_eq!(*SliceView::first(&values[..]), 41);
}
