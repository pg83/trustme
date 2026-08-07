macro_rules! define_value {
    () => {
        #[cfg(false)]
        compile_error!("cfg(false) item was enabled");

        #[cfg(not(false))]
        fn value() -> i32 {
            42
        }
    };
}

define_value!();

fn main() {
    assert_eq!(value(), 42);
}
