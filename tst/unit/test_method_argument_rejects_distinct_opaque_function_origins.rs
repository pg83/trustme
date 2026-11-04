//@ compile-fail: error
fn first() -> impl Copy { 227u16 }
fn second() -> impl Copy { 229u16 }
struct Holder<T>(T);
trait Accept<T> { fn accept(&self, value: T) -> u16; }
impl<T: Copy> Accept<T> for Holder<T> {
    fn accept(&self, _: T) -> u16 { 233 }
}
fn main() {
    let _ = Holder(first()).accept(second());
}
