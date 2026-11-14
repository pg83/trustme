struct Pair<const A: usize, const B: usize>;

trait Equal {}

impl<const N: usize> Equal for Pair<N, N> {}

trait Fixed {
    const VALUE: usize;
}

impl Fixed for Pair<9, 9> {
    const VALUE: usize = 18;
}

struct Gate;

impl Gate {
    fn check<T>(&self) -> usize
    where
        T: Equal,
        T: Fixed,
    {
        T::VALUE
    }
}

fn main() {
    assert_eq!(Gate.check::<Pair<_, _>>(), 18);
}
