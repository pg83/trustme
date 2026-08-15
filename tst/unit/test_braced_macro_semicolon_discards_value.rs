macro_rules! option {
    {} => {
        Some(0)
    };
}

fn tail_value() -> Option<i32> {
    option! {}
}

fn main() {
    option! {};
    assert_eq!(tail_value(), Some(0));
}
