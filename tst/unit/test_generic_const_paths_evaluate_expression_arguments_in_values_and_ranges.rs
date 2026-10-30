#![feature(generic_const_items)]
#![allow(incomplete_features)]
const BOUND<const N: u32>: u32 = N;
struct Limits;
impl Limits { const BOUND<const N: u32>: u32 = N; }
trait Limit { const BOUND<const N: u32>: u32; }
impl Limit for Limits { const BOUND<const N: u32>: u32 = N; }
fn classify(value: u32) -> u8 {
    match value {
        ..=BOUND::<{0 + 0}> => 3,
        BOUND::<{1 + 1}>..=Limits::BOUND::<{2 + 2}> => 1,
        <Limits as Limit>::BOUND::<{3 + 2}>.. => 2,
        _ => 0,
    }
}
fn main() {
    assert_eq!(BOUND::<{2 + 1}>, 3);
    assert_eq!(Limits::BOUND::<{3 + 1}>, 4);
    assert_eq!(<Limits as Limit>::BOUND::<{4 + 1}>, 5);
    assert_eq!(classify(0), 3);
    assert_eq!(classify(1), 0);
    assert_eq!(classify(2), 1);
    assert_eq!(classify(4), 1);
    assert_eq!(classify(5), 2);
}
