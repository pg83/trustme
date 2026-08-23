trait Three {
    type A;
    type B;
    type C;
}

fn assert_copy<T: Copy>() {}

trait Case<E>
where
    E: Three<
        A: Iterator<Item: Copy>,
        B: Iterator<Item: Copy>,
        C: Iterator<Item: Copy>,
    >,
{
    fn check() {
        assert_copy::<<E::A as Iterator>::Item>();
        assert_copy::<<E::B as Iterator>::Item>();
        assert_copy::<<E::C as Iterator>::Item>();
    }
}

fn main() {}
