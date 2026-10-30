#![feature(trait_alias)]
trait FunctionShapes = Sized where
    fn(u8) -> u8: Copy,
    unsafe fn(u8) -> u8: Copy,
    unsafe extern "C" fn(u8) -> u8: Copy,
    unsafe extern "C" fn(u8, ...) -> u8: Copy,
    unsafe extern "C" fn(u16) -> u8: Copy,
    unsafe extern "C" fn(u8) -> u16: Copy;
fn accept<T: FunctionShapes>(value: T) -> T { value }
fn increment(value: u8) -> u8 { value + 1 }
fn main() {
    let call: fn(u8) -> u8 = accept(increment as fn(u8) -> u8);
    assert_eq!(call(8), 9);
    assert_eq!(accept(17u16), 17);
}
