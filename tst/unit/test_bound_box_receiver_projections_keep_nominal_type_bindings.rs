//@ compile-variant: -O
trait Identity { type Output; }
struct Value(u32);
impl Identity for Value { type Output = Self; }
impl Value {
    fn selected(self: Box<<Self as Identity>::Output>) -> u32 { self.0 }
}
fn main() { assert_eq!(Box::new(Value(113)).selected(), 113); }
