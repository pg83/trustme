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

fn check<T: Copy>() {
    let mut values = make::<T>();
    assert!(values.next().is_some());
}

fn main() {
    check::<u8>();
}
