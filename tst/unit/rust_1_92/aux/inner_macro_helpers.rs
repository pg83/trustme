// approx's shape: an exported assertion macro with `local_inner_macros`
// hands the name of a comparison macro to a helper macro that invokes it.
#[macro_export]
macro_rules! close_to {
    ($a:expr, $b:expr) => {
        ($a - $b) * ($a - $b) < 1
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! __assert_with {
    ($check:ident, $a:expr, $b:expr) => {
        assert!($check!($a, $b), "{} failed", stringify!($check))
    };
}

#[macro_export(local_inner_macros)]
macro_rules! assert_close_to {
    ($a:expr, $b:expr) => {
        __assert_with!(close_to, $a, $b)
    };
}
