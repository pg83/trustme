macro_rules! show { ($ty:ty) => { stringify!($ty) }; }
macro_rules! unit { () => { () }; }

fn main() {
    assert_eq!(show!(()), "()");
    assert_eq!(show!(!), "!");
    assert_eq!(show!(unit!()), "unit!()");
    assert_eq!(show!(_), "_");
    assert!(show!(fn(u8, u16)).contains("u8, u16"));
    assert!(show!([u8; _]).contains("_"));
    let variadic = show!(unsafe extern "C" fn(u16, ...) -> !);
    assert!(variadic.contains("unsafe"));
    assert!(variadic.contains("extern \"C\""));
    assert!(variadic.contains("u16"));
    assert!(variadic.contains("..."));
    assert!(variadic.ends_with("-> !"));
    let no_fixed = show!(unsafe extern "C" fn(...) -> ());
    assert!(no_fixed.contains("fn(...)"));
}
