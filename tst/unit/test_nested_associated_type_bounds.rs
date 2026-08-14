trait First {
    type Assoc;
}

trait Second {
    type Assoc;
}

trait Marker {}

fn require<T: First<Assoc: Second<Assoc: Marker>>>() {}

fn main() {}
