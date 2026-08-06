// Extracted from library/alloc/src/fmt.rs:548
#![allow(unused)]
#![allow(unused_must_use)]
extern crate alloc;
fn main() {
    use std::fmt;
    use std::io::{self, Write};
    
    let mut some_writer = io::stdout();
    write!(&mut some_writer, "{}", format_args!("print with a {}", "macro"));
    
    fn my_fmt_fn(args: fmt::Arguments<'_>) {
        write!(&mut io::stdout(), "{args}");
    }
    my_fmt_fn(format_args!(", or a {} too", "function"));
}
