macro_rules! sizes {
    ($( $($name:ident)? ),+ ; done) => { [$( { $(let _ = stringify!($name);)? 1u16 } ),+] };
}
macro_rules! maybe_sizes {
    ($( $($name:ident)? ),* ; done) => { [$( { $(let _ = stringify!($name);)? 1u16 } ),*] };
}
fn main() {
    assert_eq!(sizes!(; done), [1u16]);
    assert_eq!(sizes!(,; done), [1u16, 1]);
    assert_eq!(sizes!(alpha,,beta; done), [1u16, 1, 1]);
    assert_eq!(maybe_sizes!(,; done), [1u16, 1]);
    assert_eq!(maybe_sizes!(alpha,,beta; done), [1u16, 1, 1]);
}
