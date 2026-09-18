/* A procedural macro that moves the caller's tokens into code of its own: the
   names in them still belong to the caller. `rstest::timeout` writes

       select! { () = async { Delay::new(timeout).await; }.fuse() => ..., }

   inside a function whose parameter is `timeout`, and `futures_util::select!` is
   a `macro_rules!` that opens a block, brings a name into it and hands the rest
   of the caller's tokens to the `select_internal!` procedural macro. Trustme
   reported `Couldn't find variable name 'timeout'`, because the tokens came back
   from the macro with no resolution context at all. */
use call_site_locals_macro::{after_a_local_of_its_own, pick_only};

macro_rules! through_macro_rules {
    ($($tokens:tt)*) => {{
        use core::option::Option as __private;
        let _unused: __private<u8> = __private::None;
        pick_only!($($tokens)*)
    }};
}

fn direct(timeout: u32) -> u32 {
    pick_only!(timeout + 1)
}

fn wrapped(timeout: u32) -> u32 {
    through_macro_rules!(timeout + 2)
}

fn through_a_local(timeout: u32) -> u32 {
    let step = 10;
    pick_only!(timeout + step)
}

/* The macro's own `let __value` is written at its call site, so the caller's
   tokens after it see the macro's binding and not the caller's. */
fn a_local_of_the_macros_is_in_scope() -> u32 {
    let __value = 1u32;
    after_a_local_of_its_own!(__value)
}

fn main() {
    assert_eq!(direct(7), 8);
    assert_eq!(wrapped(7), 9);
    assert_eq!(through_a_local(7), 17);
    assert_eq!(a_local_of_the_macros_is_in_scope(), 100);
}
