// typewit 1.15: `__trailing_comma_for_where_clause!{ ($crate::m!(..)) .. }`
// matches the call with `$($macro:ident)::* !(..)`. `$crate` is a single
// identifier token (rustc's `kw::DollarCrate`), so it matches `ident` and is
// one `tt` when it comes from another crate's macro as well.
#[macro_export]
macro_rules! call_with {
    (($($macro:ident)::* !($($args:tt)*)) []) => {
        $($macro)::* !{$($args)*}
    };
}

#[macro_export]
macro_rules! inner {
    ($e:expr) => {
        $e + 1
    };
}

#[macro_export]
macro_rules! outer {
    ($e:expr) => {
        $crate::call_with! { ($crate::inner!($e)) [] }
    };
}

#[macro_export]
macro_rules! first_tt {
    ($first:tt $($rest:tt)*) => {
        $crate::count_tts!($($rest)*)
    };
}

#[macro_export]
macro_rules! count_tts {
    () => { 0 };
    ($one:tt $($rest:tt)*) => { 1 + $crate::count_tts!($($rest)*) };
}

#[macro_export]
macro_rules! crate_path_tts {
    () => {
        $crate::first_tt!($crate :: inner)
    };
}
