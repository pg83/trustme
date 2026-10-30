#![feature(specialization)]
#![allow(incomplete_features)]
trait Factory { fn code()->u8; fn source()->u8; }
impl Factory for fn()->[u16;5] { fn code()->u8 {3} }
impl<T> Factory for fn()->T {
 default fn code()->u8 {1}
 default fn source()->u8 {11}
}
impl<const N:usize> Factory for fn()->[u16;N] {
 default fn code()->u8 {2}
 default fn source()->u8 {23}
}
fn main() {
 assert_eq!(<fn()->u8 as Factory>::source(),11);
 assert_eq!(<fn()->[u16;2] as Factory>::code(),2);
 assert_eq!(<fn()->[u16;5] as Factory>::code(),3);
 assert_eq!(<fn()->[u16;5] as Factory>::source(),23);
}
