//@ compile-fail: Unexpected token
macro_rules! import { ($module:ty) => { use $module::size_of; }; }
import!(std::mem);
fn main() {}
