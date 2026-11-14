trait Tag<const N: usize> {}

struct Token;

impl Tag<7> for Token {}

struct Holder<T>(T);

impl<T: Tag<7>> Holder<T> {
    fn number() -> usize {
        7
    }
}

fn main() {
    assert_eq!(Holder::<Token>::number(), 7);
}
