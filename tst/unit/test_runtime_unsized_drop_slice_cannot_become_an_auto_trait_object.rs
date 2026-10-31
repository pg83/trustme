//@ compile-fail: Type mismatch
struct Leaf;
impl Drop for Leaf { fn drop(&mut self) {} }
fn main() { let values: [Leaf; 0] = []; let slice: &[Leaf] = &values; let object: &dyn Send = slice; let _ = object; }
