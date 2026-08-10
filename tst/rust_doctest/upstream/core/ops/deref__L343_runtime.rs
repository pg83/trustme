// Extracted from library/core/src/ops/deref.rs:343
use std::boxed::Box;
use std::rc::Rc;

// Both `Box` and `Rc` (indirectly) implement Receiver

struct MyContainedType;

fn main() {
  let t = Rc::new(Box::new(MyContainedType));
  t.method_a();
  t.method_b();
  t.method_c();
}

impl MyContainedType {
  fn method_a(&self) {

  }
  fn method_b(self: &Box<Self>) {

  }
  fn method_c(self: &Rc<Box<Self>>) {

  }
}
