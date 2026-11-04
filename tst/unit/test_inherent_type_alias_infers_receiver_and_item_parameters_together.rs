#![feature(inherent_associated_types)]
#![allow(incomplete_features)]
struct Buffer<T>(T);
impl<T> Buffer<T> { type View<U> = (T, U); }
fn main() {
    let pair: <Buffer<_>>::View<_> = (769u16, 773u32);
    let first: u16 = pair.0;
    let second: u32 = pair.1;
    assert_eq!(first as u32 + second, 1542);
}
