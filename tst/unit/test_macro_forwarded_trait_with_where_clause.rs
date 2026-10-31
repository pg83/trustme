macro_rules! item { ($item:item) => { $item }; }
item! { trait Read<T> where T: Copy { type Value; const BIAS: u16 = 7; fn read(&self) -> Self::Value; } }
struct Value(u16);
impl Read<u16> for Value { type Value=u16; fn read(&self) -> u16 { self.0 + Self::BIAS } }
fn main() { assert_eq!(Value(30).read(),37); }
