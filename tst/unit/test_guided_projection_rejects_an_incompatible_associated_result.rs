//@ compile-fail: Type mismatch
trait Load { type Output; fn load(self) -> Self::Output; }
impl Load for &u16 { type Output = u32; fn load(self) -> u32 { *self as u32 } }
fn load<T: Load>(values: [T; 1]) -> T::Output { let [value] = values; value.load() }
fn main() { let n = 23u16; let values = [&n]; let _: u64 = load(values); }
