// Extracted from library/core/src/macros/mod.rs:1085
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let key: Option<&'static str> = option_env!("SECRET_KEY");
        println!("the secret key might be: {key:?}");
        Ok(())
    }
    doctest().unwrap();
}
