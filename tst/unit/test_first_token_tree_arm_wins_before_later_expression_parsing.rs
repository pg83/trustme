macro_rules! first {
    ($($tokens:tt)*) => { 37u8 };
    ($expression:expr) => { 91u8 };
}
fn main() {
    assert_eq!(first!(for), 37);
    assert_eq!(first!(for value), 37);
    assert_eq!(first!(for value in), 37);
    assert_eq!(first!(for value in 0..3), 37);
    assert_eq!(first!(match), 37);
    assert_eq!(first!(match value), 37);
    assert_eq!(first!(while let), 37);
    assert_eq!(first!(while let value), 37);
    assert_eq!(first!(while flag), 37);
    assert_eq!(first!(loop), 37);
    assert_eq!(first!(if), 37);
    assert_eq!(first!(if flag), 37);
    assert_eq!(first!(if flag {} else), 37);
    assert_eq!(first!(|value), 37);
    assert_eq!(first!(|| ->), 37);
    assert_eq!(first!(const), 37);
    assert_eq!(first!(unsafe), 37);
    assert_eq!(first!('label), 37);
    assert_eq!(first!('label: value), 37);
    assert_eq!(first!(value.method::), 37);
    assert_eq!(first!(value.method::<), 37);
    assert_eq!(first!(value.), 37);
    assert_eq!(first!(#![allow(unused)] value), 37);
    assert_eq!(first!(value: u32), 37);
    assert_eq!(first!(return +), 37);
    assert_eq!(first!(::), 37);
    assert_eq!(first!(macro_rules! nested { () => { 17 } }), 37);
}
