macro_rules! record {
    ($parameter:ident, $field:ty) => {
        #[derive(Clone)]
        struct Repeated<$parameter> { left: $field, right: $field }
    };
}

record!(T, Option<T>);

fn main() {
    let original = Repeated { left: Some(11u16), right: Some(17) };
    let cloned = original.clone();
    assert_eq!((cloned.left, cloned.right), (Some(11), Some(17)));
    assert_eq!((original.left, original.right), (Some(11), Some(17)));
}
