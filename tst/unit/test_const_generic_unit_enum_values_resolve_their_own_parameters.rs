//@ compile-variant: -O
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Flag<const N: usize> { Off, On }
const fn width<const N: usize>(_: Flag<N>) -> usize { N }
type Five = Flag<{ 1 + 4 }>;
const SELECTED: Five = Flag::<{ 2 + 3 }>::On;
const WIDTH: usize = width(Flag::<{ 2 + 3 }>::Off);
fn main() {
    assert_eq!(width(SELECTED), 5);
    assert_eq!(SELECTED, Flag::<5>::On);
    let values = [Flag::<{ 2 + 3 }>::Off, SELECTED];
    assert_eq!(values[0], Flag::Off);
    assert_eq!(std::mem::size_of::<[u8; WIDTH]>(), 5);
    assert_ne!(std::any::TypeId::of::<Flag<4>>(), std::any::TypeId::of::<Five>());
}
