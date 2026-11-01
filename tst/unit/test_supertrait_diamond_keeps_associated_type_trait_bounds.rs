trait Left: Iterator<Item: Copy> {}
trait Right: Iterator<Item: Eq> {}
trait Both: Left + Right {}
impl<I: Iterator<Item: Copy>> Left for I {}
impl<I: Iterator<Item: Eq>> Right for I {}
impl<I: Left + Right> Both for I {}

fn first<I: Both>(mut values: I) -> bool {
    let value = values.next().unwrap();
    let copy = value;
    value == copy
}

fn main() {
    assert!(first([11u16, 17].into_iter()));
    assert!(first([true, false].into_iter()));
}
