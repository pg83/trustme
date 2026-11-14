trait Family {
    type Member<T>;
}

struct Leaf;

impl Family for Leaf {
    type Member<T> = T;
}

fn make<T: Copy>() -> impl Iterator<Item = impl Family<Member<T>: Copy>> {
    std::iter::once(Leaf)
}

fn same_items<I: Iterator, J: Iterator<Item = I::Item>>(_: I, _: J) {}

fn check<T: Copy>() {
    same_items(make::<T>(), make::<T>());
}

fn main() {
    check::<u8>();
}
