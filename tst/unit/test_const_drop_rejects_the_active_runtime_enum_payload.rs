//@ compile-fail: cannot be evaluated at compile-time
struct Runtime;
impl Drop for Runtime { fn drop(&mut self) {} }
enum Choice { Empty, Held(Runtime) }
const VALUE: u32 = { let _value = Choice::Held(Runtime); 37 };
fn main() { let _ = VALUE; }
