#![feature(specialization)]
trait Read { type Output; fn read(self) -> Self::Output; }
impl<T> Read for T {
    default type Output = u8;
    default fn read(self) -> Self::Output { panic!("unselected parent") }
}
impl Read for u8 { fn read(self) -> Self::Output { self } }
impl Read for u16 { fn read(self) -> Self::Output { self as u8 } }
impl Read for i32 { fn read(self) -> Self::Output { self as u8 } }
fn main() {
    let result: u8 = 37.read();
    assert_eq!(result, 37);
}
