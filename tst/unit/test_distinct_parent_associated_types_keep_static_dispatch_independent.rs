trait Left { type Item; fn left(&self) -> Self::Item; }
trait Right { type Item; fn right(&self) -> Self::Item; }
trait Both: Left + Right {}
struct Value;
impl Left for Value {
    type Item = u8;
    fn left(&self) -> u8 { 4 }
}
impl Right for Value {
    type Item = u16;
    fn right(&self) -> u16 { 9 }
}
impl Both for Value {}
fn accepts_both<T: Both>(_: &T) {}
fn main() {
    let value = Value;
    accepts_both(&value);
    assert_eq!(<Value as Left>::left(&value), 4u8);
    assert_eq!(<Value as Right>::right(&value), 9u16);
}
