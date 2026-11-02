//@ compile-variant: -O

struct Wrap<const N: u16>;
impl<const N: u16> Wrap<N> {
    fn amount(value: u16) -> u16 { value + N }
    const METHOD: fn(u16) -> u16 = Self::amount;
}
trait Calculate {
    const OFFSET: u16;
    fn calculate(value: u16) -> u16;
}
impl<const N: u16> Calculate for Wrap<N> {
    const OFFSET: u16 = N + 100;
    fn calculate(value: u16) -> u16 { value * N }
}

fn invoke<F: Fn(u16) -> u16>(call: F, value: u16) -> u16 { call(value) }

fn main() {
    let inherent: fn(u16) -> u16 = Wrap::<3>::amount;
    let associated: fn(u16) -> u16 = <Wrap<3> as Calculate>::calculate;
    assert_eq!(inherent(11), 14);
    assert_eq!(associated(11), 33);
    assert_eq!(Wrap::<5>::METHOD(11), 16);
    assert_eq!(invoke(Wrap::<7>::amount, 11), 18);
    assert_eq!(invoke(<Wrap<7> as Calculate>::calculate, 11), 77);
    assert_eq!(<Wrap<7> as Calculate>::OFFSET, 107);
}
