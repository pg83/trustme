//@ compile-fail: Type mismatch
struct Leaf;
impl Drop for Leaf { fn drop(&mut self) {} }
const SLICE: &[Leaf] = &[];
const OBJECT: &dyn Send = SLICE;
fn main() { let _ = OBJECT; }
