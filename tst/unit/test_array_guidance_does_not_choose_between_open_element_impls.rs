//@ compile-fail: type annotations needed
trait Load { type Output; fn load(self) -> Self::Output; }
impl Load for &Vec<u16> { type Output = u16; fn load(self) -> u16 { 16 } }
impl Load for &Vec<u32> { type Output = u32; fn load(self) -> u32 { 32 } }
fn load<T: Load>(values: [T; 1]) -> T::Output { let [value] = values; value.load() }
fn main() { let values = [&Vec::new()]; let _ = load(values); }
