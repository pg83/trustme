//@ proc-macro-aux-build: numeric_token_bridge.rs
use numeric_token_bridge::intact;
#[intact]
mod arithmetic {
    pub(crate) const BASE: u32 = 19;
    static MASK: u32 = 15;
    #[derive(Debug, PartialEq)]
    pub enum ResultKind { Value(u32), Empty }
    pub union Bits { pub word: u32, pub bytes: [u8; 4] }
    pub trait Measure { type Output; fn measure(&self) -> Self::Output; }
    pub struct Value(pub u32);
    impl Measure for Value {
        type Output = u32;
        fn measure(&self) -> u32 { self.0 }
    }
    extern "C" fn step(value: u32) -> u32 { value + 1 }
    pub fn evaluate() -> ResultKind {
        let mut value = ((BASE + 5) - 2) * 3 / 6 % 10;
        value <<= 2; value >>= 1; value |= 8; value &= 14; value ^= 3;
        value += 1; value -= 2; value *= 3; value /= 2; value %= 5;
        let complement = !value & MASK;
        let mut total = 0;
        for n in 0..=3 { if n == 1 { continue; } total += n; }
        while total < 7 { total += 1; }
        loop { total += 1; break; }
        let pair = (value, complement);
        let selected = match pair { (n @ 2, tail) if tail >= 13 && tail <= 13 => n + tail, _ => return ResultKind::Empty };
        assert!(value != 0 && value > 1 || value == BASE);
        let values = [selected, total];
        let view = &values[..];
        let bits = Bits { word: view[0] + view[1] };
        let callback: extern "C" fn(u32) -> u32 = step;
        let measured = <Value as Measure>::measure(&Value(unsafe { bits.word }));
        let raw = measured.to_ne_bytes();
        assert_eq!(unsafe { Bits { bytes: raw }.word }, measured);
        ResultKind::Value(callback(measured))
    }
}
fn main() { assert_eq!(arithmetic::evaluate(), arithmetic::ResultKind::Value(24)); }
