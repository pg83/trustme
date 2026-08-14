macro_rules! one_token {
    ($token:tt) => {
        1
    };
}

macro_rules! one_literal {
    ($literal:literal) => {
        2
    };
}

fn main() {
    assert_eq!(one_token!("string"suffix), 1);
    assert_eq!(one_literal!(1suffix), 2);
}
