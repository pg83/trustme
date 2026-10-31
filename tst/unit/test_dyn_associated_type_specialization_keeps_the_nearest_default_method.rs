#![feature(specialization)]
#![allow(incomplete_features)]
trait Family { type Item; }
trait Score { const ID: u8; fn code() -> u8; }
impl<T> Score for &(dyn Family<Item = T> + Send + Sync) {
    default const ID: u8 = 1;
    default fn code() -> u8 { Self::ID + 10 }
}
impl<T> Score for &(dyn Family<Item = (T, T)> + Send + Sync) {
    default const ID: u8 = 2;
    default fn code() -> u8 { Self::ID + 20 }
}
impl Score for &(dyn Family<Item = (u32, u32)> + Send + Sync) { const ID: u8 = 7; }
fn main() {
    type Selected = dyn Family<Item = (u32, u32)> + Send + Sync;
    type Middle = dyn Family<Item = (u16, u16)> + Send + Sync;
    type Base = dyn Family<Item = u8> + Send + Sync;
    assert_eq!(<&Selected as Score>::code(), 27);
    assert_eq!(<&Middle as Score>::code(), 22);
    assert_eq!(<&Base as Score>::code(), 11);
}
