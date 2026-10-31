//@ compile-fail: Type mismatch
struct Leaf;
impl Drop for Leaf { fn drop(&mut self) {} }
fn main() { let values: [Leaf; 0] = []; let source: *const [Leaf] = &values; let object = source as *const dyn Send; let _ = object; }
