//@ compile-fail: Unexpected token
macro_rules! import { ($module:ty) => { use $module::*; }; }
import!((u8, u16));
fn main() {}
