// Extracted from src/names/scopes.md:309
#![allow(unused)]
fn main() {
    // Self type within struct definition.
    struct Recursive {
        f1: Option<Box<Self>>
    }
    
    // Self type within generic parameters.
    struct SelfGeneric<T: Into<Self>>(T);
    
    // Self value constructor within an implementation.
    struct ImplExample();
    impl ImplExample {
        fn example() -> Self { // Self type
            Self() // Self value constructor
        }
    }
}
