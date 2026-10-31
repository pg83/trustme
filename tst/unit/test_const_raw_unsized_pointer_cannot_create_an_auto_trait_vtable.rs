//@ compile-fail: Type mismatch
struct Leaf;
impl Drop for Leaf { fn drop(&mut self) {} }
const SOURCE: *const [Leaf] = &[];
const OBJECT: *const dyn Send = SOURCE as *const dyn Send;
fn main() { let _ = OBJECT; }
