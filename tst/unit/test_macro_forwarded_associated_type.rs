macro_rules! member { ($item:item) => { $item }; }
trait Read { type Value; fn read(&self) -> Self::Value; }
struct Host(u16);
impl Read for Host { member!(type Value = u16;); member!(fn read(&self) -> Self::Value { self.0 }); }
fn main() { assert_eq!(Host(37).read(),37); }
