//@ proc-macro-aux-build: proc_macro_with_startup_stdout.rs

struct Token;

impl Token {
    #[proc_macro_with_startup_stdout::preserve]
    fn value() -> u8 {
        23
    }
}

fn main() {
    assert_eq!(Token::value(), 23);
}
