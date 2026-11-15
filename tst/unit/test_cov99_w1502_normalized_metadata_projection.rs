//@ aux-build: cov99_w1502_projection_constants.rs

extern crate cov99_w1502_projection_constants as shapes;

fn is_mark<T: shapes::Shape<Item = u8>>(value: T::Item) -> bool {
    match value {
        shapes::MARK => true,
        _ => false,
    }
}

fn is_pair(value: (<shapes::Byte as shapes::Shape>::Item, ())) -> bool {
    match value {
        shapes::PAIR => true,
        _ => false,
    }
}

fn main() {
    assert!(is_mark::<shapes::Byte>(7));
    assert!(!is_mark::<shapes::Byte>(8));
    assert!(is_pair((7, ())));
    assert!(!is_pair((8, ())));
}
