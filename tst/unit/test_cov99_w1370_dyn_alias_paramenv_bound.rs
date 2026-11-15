//@ compile-fail: Select
#![feature(trait_alias)]

trait Required {}
trait Other {
    type Item;
}
trait Carrier {
    type Item;
}
trait Bounded = Carrier<Item: Required>;
trait Select<T> {}

fn need<X: Select<T>, T>() {}
fn probe<U: Other, T, X>()
where
    X: Select<<dyn Bounded<Item = U::Item> as Carrier>::Item>,
{
    need::<X, T>();
}
fn main() {}
