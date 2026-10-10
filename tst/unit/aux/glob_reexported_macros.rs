//@ edition: 2021
#[doc(hidden)]
#[allow(non_snake_case)]
pub mod __hidden {
    pub use crate::*;
}

#[macro_export]
macro_rules! inner {
    ($e:expr) => {
        $e * 2
    };
}

#[macro_export]
macro_rules! outer {
    ($e:expr) => {{
        use $crate::__hidden;
        __hidden::inner!($e)
    }};
}
