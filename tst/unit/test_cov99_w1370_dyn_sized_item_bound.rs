//@ compile-fail: error
#![feature(trait_alias)]

trait Required {}
trait Carrier {
    type Item where Self: Sized;
}
trait Bounded = Carrier<Item: Required>;
trait Select<T> {}
impl Select<<dyn Bounded as Carrier>::Item> for () {}

fn need<T: Select<u8>>() {}
fn main() {
    need::<()>();
}
