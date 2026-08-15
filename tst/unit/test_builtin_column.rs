macro_rules! expanded_column {
    () => {
        column!()
    };
}

fn main() {
    let actual = column!();
    assert_eq!(actual, 18);
    let expanded = expanded_column!();
    assert_eq!(expanded, 20);
}
