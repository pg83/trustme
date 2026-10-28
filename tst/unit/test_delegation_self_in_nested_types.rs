#![feature(fn_delegation)]
#![allow(incomplete_features)]

trait Shapes: Sized {
    fn pair(value: (Self, u8)) -> (Self, u8) { value }
    fn array(value: [Self; 2]) -> [Self; 2] { value }
    fn optional(value: Option<Self>) -> Option<Self> { value }
    fn slice(value: &[Self]) -> usize { value.len() }
    fn pointers(value: *const Self) -> *const Self { value }
    fn callback(value: Self, transform: fn(Self) -> Self) -> Self { transform(value) }
    fn collect<I: Iterator<Item = Self>>(value: I) -> Vec<Self> { value.collect() }
    fn mapped(value: Self, transform: &dyn Fn(Self) -> Self) -> Self { transform(value) }
}
impl Shapes for u16 {}

reuse Shapes::{pair, array, optional, slice, pointers, callback, collect, mapped};

fn increment(value: u16) -> u16 { value + 1 }

fn main() {
    assert_eq!(pair((300u16, 7)), (300, 7));
    assert_eq!(array([300u16, 400]), [300, 400]);
    assert_eq!(optional(Some(500u16)), Some(500));
    assert_eq!(optional(None::<u16>), None);
    assert_eq!(slice(&[100u16, 200, 300]), 3);
    let value = 700u16;
    assert_eq!(pointers(&value), &value as *const _);
    assert_eq!(callback(800u16, increment), 801);
    assert_eq!(collect([10u16, 20, 30].into_iter()), vec![10, 20, 30]);
    assert_eq!(mapped(900u16, &increment), 901);
}
