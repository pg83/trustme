//@ compile-fail: Unexpected token
trait Family { type Item; }
impl Family for () { type Item = u8; }
macro_rules! import { ($module:ty) => { use $module::*; }; }
import!(<() as Family>::Item);
fn main() {}
