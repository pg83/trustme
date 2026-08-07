trait Project {
    type Output;
}

struct Pair {
    first: u8,
    second: u16,
}

impl Project for () {
    type Output = Pair;
}

fn check<T: Project<Output = Pair>>() {
    let value = T::Output {
        first: 7,
        second: 11,
    };
    let T::Output { first, second } = value;
    assert_eq!((first, second), (7, 11));
}

fn main() {
    check::<()>();
}
