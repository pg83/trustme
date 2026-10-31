#![feature(stmt_expr_attributes)]
macro_rules! keep { ($value:expr) => { $value }; }
macro_rules! expression { ($value:expr) => { keep!($value) }; }
macro_rules! block { ($value:block) => { keep!($value) }; }

fn fallible() -> Result<u32, ()> { Ok(keep!(Ok::<u32, ()>(23)?)) }

fn main() {
    assert_eq!(keep!(#[allow(unused)] 17u32), 17);
    let empty_arguments = keep!(| | 19u8);
    let borrowed = keep!(&|value: u8,| value + 3);
    assert_eq!((empty_arguments(), borrowed(23)), (19, 26));
    assert_eq!(expression!(17 + 5), 22);
    assert_eq!(block!({ 37u32 }), 37);
    assert_eq!(keep!(if true { 17u32 } else { 23 }), 17);
    assert_eq!(fallible(), Ok(23));
    let values = [11u32, 17, 23];
    assert_eq!(values[keep!(..)], [11, 17, 23]);
    assert_eq!(values[keep!(..2)], [11, 17]);
    assert_eq!(values[keep!(..=1)], [11, 17]);
    assert_eq!(keep!(3..).start, 3);
}
