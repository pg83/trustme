macro_rules! show { ($ty:ty) => { stringify!($ty) }; }

fn main() {
    let empty = show!(impl Copy + use<>);
    assert!(empty.contains("Copy"));
    assert!(empty.contains("use<>"));
    let generic = show!(impl for<'a> Fn(&'a str) -> &'a str + use<T>);
    assert!(generic.contains("for<'a>"));
    assert!(generic.contains("use<T>"));
    let lifetime = show!(impl Copy + 'a + use<'a, T>);
    assert!(lifetime.contains("Copy + 'a"));
    assert!(lifetime.contains("use<'a, T>"));
}
