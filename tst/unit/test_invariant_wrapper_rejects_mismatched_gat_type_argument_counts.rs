//@ compile-fail: Type mismatch in path params (type count)

trait Carrier {
    type Item<T>;
}

struct Wrapper<T>(T);

fn compare<C: Carrier>(value: Wrapper<C::Item<u8>>) {
    let _: Wrapper<C::Item<u8, u16>> = value;
}

fn main() {}
