// Extracted from src/type-coercions.md:38
fn bar(_: &i8) { }

  fn main() {
      bar(&mut 42);
  }
