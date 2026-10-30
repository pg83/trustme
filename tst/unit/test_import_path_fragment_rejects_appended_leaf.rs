//@ compile-fail: Unexpected token
macro_rules! import { ($module:path) => { use $module::size_of; }; }
import!(std::mem);
fn main() {}
