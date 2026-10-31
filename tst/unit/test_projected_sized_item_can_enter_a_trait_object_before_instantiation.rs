trait Value { fn value(&self) -> u32; }
impl Value for u32 { fn value(&self) -> u32 { *self } }
trait Family { type Item: Value; }
struct Selected;
impl Family for Selected { type Item = u32; }
fn erase<T: Family>(value: &T::Item) -> &dyn Value { value }
fn main() { assert_eq!(erase::<Selected>(&37u32).value(), 37); }
