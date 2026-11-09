//@ compile-fail: Unexpected const generic argument

trait Carrier {
    type Item<const N: usize>;
}

struct Wrapper<T>(T);

fn compare<C: Carrier>(value: Wrapper<C::Item<1>>) {
    let _: Wrapper<C::Item<1, 2>> = value;
}

fn main() {}
