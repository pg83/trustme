struct EmptyTuple();

enum Kind {
    Unit,
    EmptyTuple(),
}

fn main() {
    let _ = EmptyTuple {};
    let _ = Kind::Unit {};
    let _ = Kind::EmptyTuple {};
}
