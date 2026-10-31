//@ compile-fail: cannot be evaluated at compile-time
struct Runtime;
impl Drop for Runtime { fn drop(&mut self) {} }
const VALUE: u32 = { let _tuple = (37u8, (Runtime, false), [41u16; 2]); 19 };
fn main() { let _ = VALUE; }
