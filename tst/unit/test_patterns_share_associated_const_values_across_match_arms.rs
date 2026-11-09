const MARK: u8 = b'\x09';

trait Value {
    const VALUE: u8;
}

impl Value for u8 {
    const VALUE: u8 = MARK + 4;
}

enum Shape {
    Pair(u8, u8),
    Empty,
}

fn classify<T: Value>(pair: (u8, u8), shape: Shape) -> u8 {
    let (head, ..) = pair;
    let value = match shape {
        Shape::Pair(MARK, value @ 2..=4) => value,
        Shape::Pair(_, other) => other,
        Shape::Empty => 0,
    };
    let [start, rest @ ..] = [head, value, T::VALUE];
    start + rest[0] + rest[1]
}

fn main() {
    assert_eq!(classify::<u8>((5, 99), Shape::Pair(MARK, 3)), 21);
    assert_eq!(classify::<u8>((5, 99), Shape::Pair(0, 7)), 25);
    assert_eq!(classify::<u8>((5, 99), Shape::Empty), 18);
}
