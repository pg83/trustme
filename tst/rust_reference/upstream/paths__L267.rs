// Extracted from src/paths.md:267
fn foo() {}
fn bar() {
    self::foo();
}
struct S(bool);
impl S {
  fn baz(self) {
        self.0;
    }
}
fn main() {}
