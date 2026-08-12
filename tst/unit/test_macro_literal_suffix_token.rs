macro_rules! one_tt {
    ($value:tt) => {};
}

macro_rules! one_literal {
    ($value:literal) => {};
}

fn main() {
    one_tt!("text"suffix);
    one_literal!(1suffix);
}
