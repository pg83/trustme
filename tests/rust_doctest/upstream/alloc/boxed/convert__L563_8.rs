// Extracted from library/alloc/src/boxed/convert.rs:563
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::error::Error;
    use std::fmt;

    #[derive(Debug)]
    struct AnError;

    impl fmt::Display for AnError {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "An error")
        }
    }

    impl Error for AnError {}

    unsafe impl Send for AnError {}

    unsafe impl Sync for AnError {}

    let an_error = AnError;
    assert!(0 == size_of_val(&an_error));
    let a_boxed_error = Box::<dyn Error + Send + Sync>::from(an_error);
    assert!(
        size_of::<Box<dyn Error + Send + Sync>>() == size_of_val(&a_boxed_error))
}
