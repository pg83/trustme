// Extracted from library/core/src/fmt/mod.rs:2372
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::fmt;
        use std::marker::PhantomData;
        
        struct Foo<T>(i32, String, PhantomData<T>);
        
        impl<T> fmt::Debug for Foo<T> {
            fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt.debug_tuple("Foo")
                    .field(&self.0)
                    .field(&self.1)
                    .field(&format_args!("_"))
                    .finish()
            }
        }
        
        assert_eq!(
            "Foo(10, \"Hello\", _)",
            format!("{:?}", Foo(10, "Hello".to_string(), PhantomData::<u8>))
        );
        Ok(())
    }
    doctest().unwrap();
}
