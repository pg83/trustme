macro_rules! expr {
    ($e:expr) => {};
}

fn main() {
    let full = ..;
    let inclusive = ..=10;
    let borrowed_full = &..;
    let borrowed_inclusive = &..=10;

    assert_eq!(full, ..);
    assert_eq!(inclusive, ..=10);
    assert_eq!(*borrowed_full, ..);
    assert_eq!(*borrowed_inclusive, ..=10);

    let nested_range = &[..=..][..];
    assert_eq!(nested_range.len(), 1);

    expr!(!..0);
    expr!(-..0);
    expr!(*..0);
    expr!(0 + ..4);
}
