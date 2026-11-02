//@ compile-fail: mismatch
trait First { fn value(input: u8) -> u8; }
trait Second { fn value(input: u8) -> u8; }
struct Operations;
impl First for Operations { fn value(input: u8) -> u8 { input + 1 } }
impl Second for Operations { fn value(input: u8) -> u8 { input + 2 } }
fn require_same<T>(_: T, _: T) {}

fn main() {
    require_same(<Operations as First>::value, <Operations as Second>::value);
}
