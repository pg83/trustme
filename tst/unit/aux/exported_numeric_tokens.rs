#[macro_export]
macro_rules! exported_unsigned {
    ($callback:ident) => { $callback!((0xff_ffu16, 18446744073709551616u128)) };
}
#[macro_export]
macro_rules! exported_signed {
    ($callback:ident) => { $callback!((-0x7fi8, -170141183460469231731687303715884105728i128)) };
}
#[macro_export]
macro_rules! exported_float {
    ($callback:ident) => { $callback!(1.000000000000000000000000000001f128) };
}
