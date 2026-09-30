//@ aux-build: matches.rs
// async-compression re-exports `pub use compression_core as core;` and then
// `pub use core::Level;`. In a 2018 use path the first segment is looked up
// in the module's own names before the extern prelude, so `core` there is
// the re-export, not the `core` crate. We took any name of the extern
// prelude as the crate first and failed to find `core::Level`. Only the
// type namespace counts: tracing-subscriber imports the `thread_local!`
// macro with `use std::thread_local;` beside `use thread_local::ThreadLocal;`
// from the crate of that name.
pub mod compression_core {
    #[derive(Debug, PartialEq)]
    pub struct Level(pub u8);
}
pub use compression_core as core;
pub use core::Level;
mod shadowed_by_a_macro {
    use std::matches;
    use matches::Pattern;

    pub fn check() -> bool {
        matches!(Some(Pattern(1)), Some(Pattern(1)))
    }
}

fn main() {
    assert!(shadowed_by_a_macro::check());
    assert_eq!(Level(3), core::Level(3));
    assert_eq!(::core::mem::size_of::<Level>(), 1);
}
