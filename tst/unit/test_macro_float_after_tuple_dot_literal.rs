macro_rules! take_literal {
    (.$literal:literal) => {};
}

fn main() {
    take_literal!(.0.0);
}
