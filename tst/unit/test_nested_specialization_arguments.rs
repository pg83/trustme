#![feature(specialization)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
struct Envelope<T>(PhantomData<T>);
struct Engine;
trait Depth<P> {
    const CODE: u8;
    fn code() -> u8 { Self::CODE }
}
impl Depth<Envelope<([u16; 3], *const [u16], &'static [u16])>> for Engine {
    const CODE: u8 = 30;
}
impl<T: 'static, const N: usize> Depth<Envelope<([T; N], *const [T], &'static [T])>> for Engine {
    default const CODE: u8 = 10;
}
impl<T: 'static> Depth<Envelope<([T; 3], *const [T], &'static [T])>> for Engine {
    default const CODE: u8 = 20;
}
fn classify<T: 'static, const N: usize>() -> u8 {
    <Engine as Depth<Envelope<([T; N], *const [T], &'static [T])>>>::code()
}
fn main() {
    assert_eq!(classify::<u8, 4>(), 10);
    assert_eq!(classify::<u8, 3>(), 20);
    assert_eq!(classify::<u16, 3>(), 30);
    assert_eq!(classify::<u16, 4>(), 10);
}
