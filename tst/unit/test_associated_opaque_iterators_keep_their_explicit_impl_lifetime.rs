//@ compile-variant: -O
#![feature(impl_trait_in_assoc_type)]
trait Windows<'a> {
    type Items: Iterator<Item = &'a u16> + 'a;
    fn items(&'a self) -> Self::Items;
}
impl<'a> Windows<'a> for [u16; 3] {
    type Items = impl Iterator<Item = &'a u16> + 'a;
    fn items(&'a self) -> Self::Items { self.iter() }
}
fn main() {
    let values = [499u16, 503, 509];
    let mut iter = Windows::items(&values);
    assert_eq!(iter.next(), Some(&499));
    assert_eq!(iter.next(), Some(&503));
    assert_eq!(iter.next(), Some(&509));
    assert_eq!(iter.next(), None);
}
