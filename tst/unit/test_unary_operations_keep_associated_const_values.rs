//@ compile-flags: -Coverflow-checks=off
trait Settings { const VALUE: i32; const MASK: u16; const READY: bool; }
struct First;
struct Second;
impl Settings for First { const VALUE: i32 = 37; const MASK: u16 = 0x1234; const READY: bool = false; }
impl Settings for Second { const VALUE: i32 = -37; const MASK: u16 = 0xedcb; const READY: bool = true; }
fn apply<T: Settings>() -> (i32, u16, bool) {
    let value = T::VALUE;
    let mask = T::MASK;
    let ready = T::READY;
    (-value, !mask, !ready)
}
fn main() {
    assert_eq!(apply::<First>(), (-37, 0xedcb, true));
    assert_eq!(apply::<Second>(), (37, 0x1234, false));
}
