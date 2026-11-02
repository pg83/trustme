//@ compile-fail: cannot be evaluated at compile-time
struct RuntimeOnly;
impl Drop for RuntimeOnly { fn drop(&mut self) {} }
const CHECK: () = { let _guard = RuntimeOnly; };
fn main() { let _ = CHECK; }
