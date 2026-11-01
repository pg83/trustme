macro_rules! sizes {
    ($( $($name:ident)? ),+) => { [$( { $(let _ = stringify!($name);)? 1u16 } ),+] };
}
fn main() {
    assert_eq!(sizes!(), [1u16]);
    assert_eq!(sizes!(,), [1u16, 1]);
    assert_eq!(sizes!(alpha,), [1u16, 1]);
    assert_eq!(sizes!(,beta), [1u16, 1]);
    assert_eq!(sizes!(alpha,,beta), [1u16, 1, 1]);
}
