macro_rules! statement_count {
    ($($statement:stmt)*) => {
        $(
            { stringify!($statement); 1 }
        )<<*
    };
}

fn main() {
    assert_eq!(statement_count! { return || true }, 1);
    assert_eq!(statement_count! { (return) || true }, 1);
    assert_eq!(statement_count! { { return } || true }, 2);
}
