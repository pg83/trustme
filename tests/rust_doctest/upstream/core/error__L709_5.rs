// Extracted from library/core/src/error.rs:709
#![allow(unused)]
#![feature(error_generic_member_access)]
fn main() {
    
    use core::error::Request;
    use core::error::request_value;
    
    #[derive(Debug)]
    struct Parent(Option<u8>);
    
    impl std::fmt::Display for Parent {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "a parent failed")
        }
    }
    
    impl std::error::Error for Parent {
        fn provide<'a>(&'a self, request: &mut Request<'a>) {
            if let Some(v) = self.0 {
                request.provide_value::<u8>(v);
            }
        }
    }
    
    #[derive(Debug)]
    struct Child {
        parent: Parent,
    }
    
    impl Child {
        // Pretend that this takes a lot of resources to evaluate.
        fn an_expensive_computation(&self) -> Option<u8> {
            Some(99)
        }
    }
    
    impl std::fmt::Display for Child {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "child failed: \n  because of parent: {}", self.parent)
        }
    }
    
    impl std::error::Error for Child {
        fn provide<'a>(&'a self, request: &mut Request<'a>) {
            // In general, we don't know if this call will provide
            // an `u8` value or not...
            self.parent.provide(request);
    
            // ...so we check to see if the `u8` is needed before
            // we run our expensive computation.
            if request.would_be_satisfied_by_value_of::<u8>() {
                if let Some(v) = self.an_expensive_computation() {
                    request.provide_value::<u8>(v);
                }
            }
    
            // The request will be satisfied now, regardless of if
            // the parent provided the value or we did.
            assert!(!request.would_be_satisfied_by_value_of::<u8>());
        }
    }
    
    let parent = Parent(Some(42));
    let child = Child { parent };
    assert_eq!(Some(42), request_value::<u8>(&child));
    
    let parent = Parent(None);
    let child = Child { parent };
    assert_eq!(Some(99), request_value::<u8>(&child));
}
