//@ proc-macro-aux-build: late_assignment_attribute.rs
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

macro_rules! body {
    () => { compile_error!("body expanded before its attribute"); };
}
macro_rules! publish_attribute {
    () => {
        mod later {
            pub use late_assignment_attribute::replace_assignment;
        }
    };
}

fn main() {
    let mut first = 0u8;
    let mut second = 0u8;
    #[later::replace_assignment]
    body!();
    assert_eq!((first, second), (17, 19));
}

publish_attribute!();
