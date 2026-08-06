// Extracted from library/core/src/error.rs:317
#![allow(unused)]
#![feature(error_iter)]
fn main() {
    use std::error::Error;
    use std::fmt;
    
    #[derive(Debug)]
    struct A;
    
    #[derive(Debug)]
    struct B(Option<Box<dyn Error + 'static>>);
    
    impl fmt::Display for A {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "A")
        }
    }
    
    impl fmt::Display for B {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            write!(f, "B")
        }
    }
    
    impl Error for A {}
    
    impl Error for B {
        fn source(&self) -> Option<&(dyn Error + 'static)> {
            self.0.as_ref().map(|e| e.as_ref())
        }
    }
    
    let b = B(Some(Box::new(A)));
    
    // let err : Box<Error> = b.into(); // or
    let err = &b as &dyn Error;
    
    let mut iter = err.sources();
    
    assert_eq!("B".to_string(), iter.next().unwrap().to_string());
    assert_eq!("A".to_string(), iter.next().unwrap().to_string());
    assert!(iter.next().is_none());
    assert!(iter.next().is_none());
}
