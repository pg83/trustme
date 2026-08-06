// Extracted from src/subtyping.md:90
#![allow(unused)]
fn main() {
    use std::cell::UnsafeCell;
    fn generic_tuple<'short, 'long: 'short>(
        // 'long is used inside of a tuple in both a co- and invariant position.
        x: (&'long u32, UnsafeCell<&'long u32>),
    ) {
        // As the variance at these positions is computed separately,
        // we can freely shrink 'long in the covariant position.
        let _: (&'short u32, UnsafeCell<&'long u32>) = x;
    }
    
    fn takes_fn_ptr<'short, 'middle: 'short>(
        // 'middle is used in both a co- and contravariant position.
        f: fn(&'middle ()) -> &'middle (),
    ) {
        // As the variance at these positions is computed separately,
        // we can freely shrink 'middle in the covariant position
        // and extend it in the contravariant position.
        let _: fn(&'static ()) -> &'short () = f;
    }
}
