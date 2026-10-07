// std 1.92's `sys::pal::unix::weak::dlsym::weak` is a `macro` whose body calls
// `panic!`. A name with the macro's definition-site hygiene is looked up in
// the defining module and then in the macro_use prelude, which holds what
// `#[macro_use] extern crate std` brings in.
#![feature(decl_macro)]

mod weak {
    pub(crate) macro checked($text:expr) {
        {
            let Some(len) = Some($text.len()) else { panic!("never") };
            len
        }
    }

    pub(crate) macro listed($($x:expr),*) {
        vec![$($x),*]
    }
}

fn main() {
    assert_eq!(weak::checked!("abc"), 3);
    assert_eq!(weak::listed!(1, 2), [1, 2]);
}
