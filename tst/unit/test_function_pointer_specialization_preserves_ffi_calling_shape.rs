#![feature(specialization)]
#![allow(incomplete_features)]
trait Origin { fn code()->u8; fn source()->u8; }
impl Origin for unsafe extern "C" fn([u8;3],...)->[u8;3] { fn code()->u8 {3} }
impl<T> Origin for unsafe extern "C" fn(T,...)->T {
 default fn code()->u8 {1}
 default fn source()->u8 {13}
}
impl<const N:usize> Origin for unsafe extern "C" fn([u8;N],...)->[u8;N] {
 default fn code()->u8 {2}
 default fn source()->u8 {29}
}
impl<T> Origin for unsafe extern "C" fn(T)->T {
 default fn code()->u8 {4}
 default fn source()->u8 {37}
}
impl Origin for unsafe extern "C" fn(u16)->u16 { fn code()->u8 {5} }
fn main() {
 assert_eq!(<unsafe extern "C" fn(u8,...)->u8 as Origin>::source(),13);
 assert_eq!(<unsafe extern "C" fn([u8;2],...)->[u8;2] as Origin>::code(),2);
 assert_eq!(<unsafe extern "C" fn([u8;3],...)->[u8;3] as Origin>::source(),29);
 assert_eq!(<unsafe extern "C" fn(u16)->u16 as Origin>::code(),5);
 assert_eq!(<unsafe extern "C" fn(u16)->u16 as Origin>::source(),37);
}
