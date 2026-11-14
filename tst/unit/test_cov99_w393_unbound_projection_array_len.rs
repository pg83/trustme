trait Project {
    type Item;
}

impl Project for () {
    type Item = u8;
}

struct Pair<A, B>(A, B);

impl<T> Pair<T, T> {
    const N: usize = 3;
}

type Left = [u8; Pair::<<() as Project>::Item, u8>::N];
type Right = [u8; Pair::<u8, <() as Project>::Item>::N];

fn main() {
    let left: Left = [11; 3];
    let right: Right = [22; 3];
    assert_eq!(left[2] + right[2], 33);
}
