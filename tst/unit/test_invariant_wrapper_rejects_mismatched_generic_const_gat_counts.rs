//@ compile-fail: Type mismatch in path params (value count)

trait Carrier {
    type Item<const N: usize>;
}

struct Wrapper<T>(T);

fn compare<C: Carrier, const N: usize, const M: usize>(value: Wrapper<C::Item<N>>) {
    let _: Wrapper<C::Item<N, M>> = value;
}

fn main() {}
