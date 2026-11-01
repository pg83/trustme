#![feature(trait_alias)]
fn main() {
    trait Allowed = Copy;
    use Allowed as Chosen;
    fn keep<T: Chosen>(value: T) -> (T, T) { (value, value) }
    assert_eq!(keep(97u16), (97, 97));
}
