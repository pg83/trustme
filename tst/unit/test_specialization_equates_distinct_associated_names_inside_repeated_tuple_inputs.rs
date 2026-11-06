#![feature(specialization)]
#![allow(incomplete_features)]
type Early = <((u8,), (u8,), Token) as Select>::Output;
trait Family { type First; type Second; }
struct Token;
impl Family for Token { type First = u8; type Second = u8; }
trait Select { type Output; const CODE: u8; }
impl<A, B> Select for (A, A, B) {
    default type Output = u16;
    default const CODE: u8 = 1;
}
impl<T: Family<Second = <T as Family>::First>> Select for ((T::First,), (T::Second,), T) {
    type Output = u32;
    const CODE: u8 = 3;
}
fn main() {
    let value: Early = 47u32;
    assert_eq!(value, 47);
    assert_eq!(<((u8,), (u8,), Token) as Select>::CODE, 3);
}
