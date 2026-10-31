trait Load { type Output; fn load(self) -> Self::Output; }
impl Load for &u16 { type Output = u64; fn load(self) -> u64 { *self as u64 + 300 } }
fn load<T: Load<Output = u64>>(xs: [T; 1]) -> u64 { let [x] = xs; x.load() }
fn main() { let n = 19u16; let xs = [&n]; assert_eq!(load(xs), 319); }
