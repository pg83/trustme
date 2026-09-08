//@ edition: 2018
// cfg-if 1.0's `cfg_if!`, a 2018-edition crate: the items it is handed pass
// through two nested invocations of its own before they are emitted.
#[macro_export]
macro_rules! cfg_if {
    ($(
        if #[cfg($meta:meta)] { $($tokens:tt)* }
    ) else * else {
        $($tokens2:tt)*
    }) => {
        $crate::cfg_if! {
            @__items
            () ;
            $( ( ($meta) ($($tokens)*) ), )*
            ( () ($($tokens2)*) ),
        }
    };
    (@__items ($($not:meta,)*) ; ) => {};
    (@__items ($($not:meta,)*) ; ( ($($m:meta),*) ($($tokens:tt)*) ), $($rest:tt)*) => {
        #[cfg(all($($m,)* not(any($($not),*))))] $crate::cfg_if! { @__identity $($tokens)* }
        $crate::cfg_if! { @__items ($($not,)* $($m,)*) ; $($rest)* }
    };
    (@__identity $($tokens:tt)*) => {
        $($tokens)*
    };
}
