// An array pattern of known length constrains the value being destructured to
// an array of exactly that length. Here that is the only thing that can pick
// the `Into<[usize; 3]>` impl, so the pattern's length has to reach the
// inference variable before method selection gives up.

struct Zeroes;

struct Foo<T>(T);

impl Into<[usize; 3]> for Zeroes {
    fn into(self) -> [usize; 3] {
        [7; 3]
    }
}

fn main() {
    let Foo([a, b, c]) = Foo(Zeroes.into());
    assert_eq!((a, b, c), (7, 7, 7));
}
