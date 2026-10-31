#![feature(specialization)]
#![allow(incomplete_features)]
trait Rank { type Output: Copy; fn make() -> Self::Output; }
impl Rank for u8 {
    default type Output = u16;
    fn make() -> Self::Output { unsafe { std::mem::zeroed() } }
}
fn main() {
    let value = <u8 as Rank>::make();
    assert_eq!(std::mem::size_of_val(&value), 2);
    assert_eq!(std::mem::align_of_val(&value), 2);
}
