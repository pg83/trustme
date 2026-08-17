// A closure may capture a union field, and an erased type may take part in the
// coercion ordering that picks a target type.
use std::fmt::Debug;

union Pair {
    value: u64,
    halves: (u32, u32),
}

fn cloneable() -> impl Clone + Debug {
    7u8
}

fn described() -> impl Debug {
    "seven"
}

fn main() {
    let pair = Pair { value: 42 };
    let read = || unsafe { pair.value };
    assert_eq!(read(), 42);

    let mut other = Pair { halves: (1, 2) };
    let mut write = || other.halves = (3, 4);
    write();
    assert_eq!(unsafe { other.halves }, (3, 4));

    // Two different erased types in one inference set: neither is the coercion
    // target of the other.
    let described: Vec<Box<dyn Debug>> = vec![Box::new(cloneable()), Box::new(described())];
    assert_eq!(format!("{:?}", described), r#"[7, "seven"]"#);
}
