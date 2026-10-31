//@ compile-fail: cannot be evaluated at compile-time
struct Runtime;
impl Drop for Runtime { fn drop(&mut self) {} }
const VALUE: u32 = { let _values = [Runtime, Runtime]; 37 };
fn main() { let _ = VALUE; }
