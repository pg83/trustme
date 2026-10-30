#![feature(trait_alias)]
trait Values = Iterator<Item: Copy>;
fn main() {
    let mut values: Box<dyn Values<Item = u16> + Send> = Box::new(vec![4u16, 8].into_iter());
    assert_eq!(values.next(), Some(4));
    assert_eq!(Iterator::next(&mut *values), Some(8));
    assert_eq!(values.next(), None);
    let mut text: Box<dyn Values<Item = String>> = Box::new(vec![String::from("eight")].into_iter());
    assert_eq!(Iterator::next(&mut *text), Some(String::from("eight")));
}
