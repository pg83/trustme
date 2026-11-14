//@ edition: 2021

struct Width<const N: usize>;

trait Pair {
    type First;
    type Second;
}

trait Selected {
    fn width() -> usize;
}

impl<T, const N: usize> Selected for T
where
    T: Pair<First = Width<N>, Second = Width<N>>,
{
    fn width() -> usize {
        N
    }
}

struct Sample;

impl Pair for Sample {
    type First = Width<7>;
    type Second = Width<7>;
}

fn require_selected<T: Selected>() -> usize {
    T::width()
}

fn main() {
    assert_eq!(require_selected::<Sample>(), 7);
}
