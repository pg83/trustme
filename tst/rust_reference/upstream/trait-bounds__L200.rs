// Extracted from src/trait-bounds.md:200
#![allow(unused)]
fn main() {
    use std::fmt::Debug;
    struct IsDebug<T: Debug>(T);
    // error[E0277]: `T` doesn't implement `Debug`
    fn doesnt_specify_t_debug<T>(x: IsDebug<T>) {}
}
