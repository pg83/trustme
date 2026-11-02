//@ compile-variant: -O
mod source { pub enum Shape<T> { Point(T), Void } pub use Shape::{Point as MakePoint, Void as MakeVoid}; }
fn main() {
    let point = source::MakePoint::<u16>(59);
    assert_eq!(match point { source::Shape::Point(n) => n, source::Shape::Void => 0 }, 59);
    assert!(matches!(source::MakeVoid::<u32>, source::Shape::Void));
}
