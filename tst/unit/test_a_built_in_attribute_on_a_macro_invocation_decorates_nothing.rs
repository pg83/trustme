/* A built-in attribute written on a macro invocation decorates nothing.
   Upstream's `InvocationCollector::take_first_attr` (rustc_expand/src/expand.rs)
   only ever takes `cfg`/`cfg_attr` off such a node, or a name that is not a
   built-in attribute - an attribute macro; a built-in is left on the `MacCall`
   and thrown away with it by `take_mac_call`, so nothing ever reads it, and
   `check_attributes` reports it under `unused_attributes`.

   Ours used to hand each of these to its decorator: `#[inline]` then saw an
   expression that was neither a function nor a closure and killed the compiler
   with "#[inline] should be applied to a function or closure". */

macro_rules! nothing {
    () => {};
}

macro_rules! sum_of {
    ($($value:expr),*) => { 0 $(+ $value)* };
}

fn main() {
    #[inline]
    nothing!();

    /* The `#[allow]` is inert too, so it does not quiet the `#[inline]` beside
       it - both are reported. */
    #[allow(warnings)]
    #[inline]
    nothing!();

    #[cold]
    nothing!();

    #[deprecated]
    nothing!();

    #[must_use]
    nothing!();

    /* An attribute on a *parent* of the invocation is not inert, and the level
       it sets does reach the invocation inside it. */
    #[allow(unused_attributes)]
    {
        #[inline]
        nothing!();
    }

    /* `cfg` and `cfg_attr` are the exception: they are evaluated eagerly. */
    #[cfg(not(test))]
    nothing!();
    #[cfg(test)]
    compile_error!("a `cfg`-stripped invocation must not be expanded");
    #[cfg_attr(test, cfg(test))]
    nothing!();

    /* `macro_rules!` is a definition, not an invocation: its built-in
       attributes are read as they are on any other item. */
    #[macro_export]
    macro_rules! defined_in_a_body {
        () => {
            7
        };
    }

    assert_eq!(sum_of!(1, 2, 3), 6);
    assert_eq!(defined_in_a_body!(), 7);
}
