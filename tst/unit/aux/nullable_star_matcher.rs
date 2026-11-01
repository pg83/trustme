#[macro_export]
macro_rules! sizes {
    ($( $($name:ident)? ),*) => { [$( { $(let _ = stringify!($name);)? 1u16 } ),*] };
}
