//@ compile-variant: -O
trait At { fn at(&self, index: usize) -> u16; }
trait Buffer {
    type Slice<'a>: At where Self: 'a;
    fn view(&self) -> Self::Slice<'_>;
}
struct View<'a>(&'a [u16]);
impl At for View<'_> { fn at(&self, index: usize) -> u16 { self.0[index] } }
struct Data([u16; 3]);
impl Buffer for Data {
    type Slice<'a> = View<'a> where Self: 'a;
    fn view(&self) -> View<'_> { View(&self.0) }
}
fn through<B: Buffer>(data: &B) -> u16 {
    let view = data.view();
    view.at(1) + view.at(2)
}
fn main() { assert_eq!(through(&Data([11, 43, 79])), 122); }
