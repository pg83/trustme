trait Measure<const N: usize> {
    fn measure(&self) -> usize { N }
}

impl Measure<7> for u8 {}

trait Borrowed<'a> {
    fn borrowed(&self) -> &'a u16;
}

struct View<'a>(&'a u16);

impl<'a> Borrowed<'a> for View<'a> {
    fn borrowed(&self) -> &'a u16 { self.0 }
}

fn main() {
    assert_eq!(Measure::measure(&0u8), 7);
    let value = 31u16;
    let view = View(&value);
    assert_eq!(*Borrowed::borrowed(&view), 31);
}
