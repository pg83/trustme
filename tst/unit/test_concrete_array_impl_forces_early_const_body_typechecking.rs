#![feature(const_drop_in_place, const_destruct)]
trait Digit { const WIDTH: usize; }
impl Digit for u16 { const WIDTH: usize = 6; }
const fn measure<T: Digit + Copy>(_: T) -> usize {
    let mut action = || 59u8;
    let size = std::mem::size_of_val(&action);
    unsafe { std::ptr::drop_in_place(&mut action); }
    T::WIDTH + size
}
trait Extent { const LENGTH: usize; }
impl Extent for [u8; 6] { const LENGTH: usize = 6; }
const LENGTH: usize = <[u8; measure(0)] as Extent>::LENGTH;
fn main() {
    let value = [17u8; LENGTH];
    assert_eq!(value.len(), 6);
    assert_eq!(value[5], 17);
}
