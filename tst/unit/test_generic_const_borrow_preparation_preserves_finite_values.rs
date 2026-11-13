struct Holder<const N: usize>;

impl<const N: usize> Holder<N> {
    const VALUE: usize = {
        let borrowed = &N;
        *borrowed + 1
    };
}

const FIRST: usize = Holder::<3>::VALUE;
const AGAIN: usize = Holder::<3>::VALUE;

fn main() {
    assert_eq!((FIRST, AGAIN, Holder::<7>::VALUE), (4, 4, 8));
}
