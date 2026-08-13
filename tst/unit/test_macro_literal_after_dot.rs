macro_rules! accept_literal_after_dot {
    (.$literal:literal) => {};
}

fn main() {
    accept_literal_after_dot!(.0.0);

    let nested = ((1,),);
    assert_eq!(nested.0.0, 1);
}
