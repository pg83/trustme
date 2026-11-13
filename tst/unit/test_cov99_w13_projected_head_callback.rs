struct Expected<T>(T);
trait Matches<I> {}
impl<I: Iterator> Matches<I> for Expected<I::Item> {}

fn check<I: Iterator, T: Matches<I>>(_: I, _: T) {}

fn main() {
    check(
        [1u8].into_iter()
            .filter_map(|value| value.into())
            .filter_map(|value| value.into()),
        Expected(1u8),
    );
}
