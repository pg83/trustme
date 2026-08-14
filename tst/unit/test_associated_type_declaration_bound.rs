trait Bound {
    type Assoc;
}

trait Outer {
    type Assoc: Bound<Assoc: Default>;
}

fn main() {}
