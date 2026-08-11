//@ check-pass
//@ compile-flags: -Znext-solver

trait ArrayMaker {
    type Item;

    fn make(self) -> Self::Item;
}

struct Adapter<F>(F);

impl<F, const N: usize> ArrayMaker for Adapter<F>
where
    F: FnOnce() -> [u8; N],
{
    type Item = [u8; N];

    fn make(self) -> Self::Item {
        (self.0)()
    }
}

fn make_array<const N: usize>() -> [u8; N] {
    [7; N]
}

fn inferred_array<const N: usize>() -> [u8; N] {
    Adapter(make_array::<N>).make()
}

fn main() {
    assert_eq!(inferred_array::<3>(), [7, 7, 7]);
}
