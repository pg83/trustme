trait Family {
    type Member<T>: Build<T>;
}

trait Build<T>: Sized {
    fn build(value: T) -> Self;
}

fn make<F: Family>() -> F::Member<u8> {
    F::Member::<u8>::build(1u8)
}

fn main() {}
