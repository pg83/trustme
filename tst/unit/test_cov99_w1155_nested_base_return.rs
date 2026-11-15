struct Pair {
    first: u8,
    second: u8,
}

fn from_nested_base() -> Pair {
    Pair {
        first: 1,
        ..Pair {
            first: 2,
            second: return Pair {
                first: 3,
                second: 4,
            },
        }
    }
}

fn main() {
    let value = from_nested_base();
    assert_eq!(value.first, 3);
    assert_eq!(value.second, 4);
}
