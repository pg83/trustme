struct Empty();

fn identity<T>(value: T) -> T {
    value
}

fn main() {
    let value: Empty = identity(Empty());
    let Empty() = value;
}
