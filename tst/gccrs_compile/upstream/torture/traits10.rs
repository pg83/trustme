trait Foo: Sized {
    fn get(self) -> i32;
    fn test(self) -> i32 { self.get() }
}
struct Bar(i32);
impl Foo for Bar { fn get(self) -> i32 { self.0 } }
fn main() { let _ = Bar::get(Bar(123)); let _ = Bar(123).test(); }
