#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
struct Owner<T> { value: T, tag: u16 }
impl<T: CoerceUnsized<U>, U> CoerceUnsized<Owner<U>> for Owner<T> {}
#[inline(never)]
fn convert<T: CoerceUnsized<U>, U>(value: Owner<T>) -> Owner<U> { value }
fn main() {
    let bytes = [19u8, 23, 29];
    let source = Owner { value: &bytes as *const [u8; 3], tag: 263 };
    let target: Owner<*const [u8]> = convert(source);
    assert_eq!(target.tag, 263);
    assert_eq!(unsafe { &*target.value }, &[19, 23, 29]);
}
