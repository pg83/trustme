//@ compile-fail: error:
trait Inner {
    type A;
    type B;
}

trait Select {}

impl<T, V> Select for (T, V)
where
    T: Inner<A = u16, B = u16>,
{}

fn require<S: Select>() {}

fn probe<T>()
where
    T: Inner<A = u8, B = u8> + Inner<A = u16, B = u16>,
{
    require::<(T, _)>();
}

fn main() {}
