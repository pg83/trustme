#![feature(trait_alias)]

trait Width<const N: usize> {
    fn width(&self) -> usize {
        N
    }
}

trait First<const N: usize> = Width<N>;
trait Second<const N: usize> = First<N>;

struct Marker;
impl Width<3> for Marker {}

fn through_alias<T: Second<{ 1 + 2 }>>(value: &T) -> usize {
    value.width()
}

fn main() {
    assert_eq!(through_alias(&Marker), 3);
}
