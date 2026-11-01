trait Assoc { type Item; }
struct Wrap<T>(T);
trait Mark { fn value()->u8; }
impl<T> Mark for (Wrap<T>,Wrap<T>) { fn value()->u8 { 37 } }
struct Unit;
impl Assoc for Unit { type Item = u16; }
fn witness<T:Assoc>() -> u8 where (Wrap<T::Item>,Wrap<T::Item>): Mark { <(Wrap<T::Item>,Wrap<T::Item>) as Mark>::value() }
fn main() { assert_eq!(witness::<Unit>(),37); }
