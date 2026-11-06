//@ proc-macro-aux-build: proc_macro_with_startup_stdout.rs

trait Value {
    #[proc_macro_with_startup_stdout::preserve]
    fn value() -> u8 {
        31
    }
}

struct Token;
impl Value for Token {}

fn main() {
    assert_eq!(Token::value(), 31);
}
