// Extracted from src/destructors.md:420
#![allow(unused)]
fn main() {
    fn temp() {}
      let ref x = temp(); // Binds by reference.
      x;
      let ref mut x = temp(); // Binds by mutable reference.
      x;
}
