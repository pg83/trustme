#![feature(const_type_name)]

struct Cell<const N: usize>([u8; N]);

const fn names<const N: usize>() -> (&'static str, &'static str, &'static str, &'static str) {
    (
        std::any::type_name::<fn(Cell<N>) -> ()>(),
        std::any::type_name::<fn() -> Cell<N>>(),
        std::any::type_name::<&'static [Cell<N>]>(),
        std::any::type_name::<*const Cell<N>>(),
    )
}

const FIRST: (&str, &str, &str, &str) = names::<3>();
const SECOND: (&str, &str, &str, &str) = names::<5>();

fn main() {
    assert_ne!(FIRST.0, SECOND.0);
    assert_ne!(FIRST.1, SECOND.1);
    assert_ne!(FIRST.2, SECOND.2);
    assert_ne!(FIRST.3, SECOND.3);
    assert_eq!(FIRST.0, std::any::type_name::<fn(Cell<3>) -> ()>());
    assert_eq!(SECOND.1, std::any::type_name::<fn() -> Cell<5>>());
    assert_eq!(FIRST.2, std::any::type_name::<&[Cell<3>]>());
    assert_eq!(SECOND.3, std::any::type_name::<*const Cell<5>>());
}
