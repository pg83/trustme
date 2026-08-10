// Extracted from library/core/src/error.rs:797
#![allow(unused)]
#![feature(error_generic_member_access)]
fn main() {

    use core::error::Request;
    use core::error::request_ref;

    #[derive(Debug)]
    struct Parent(Option<String>);

    impl std::fmt::Display for Parent {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "a parent failed")
        }
    }

    impl std::error::Error for Parent {
        fn provide<'a>(&'a self, request: &mut Request<'a>) {
            if let Some(v) = &self.0 {
                request.provide_ref::<str>(v);
            }
        }
    }

    #[derive(Debug)]
    struct Child {
        parent: Parent,
        name: String,
    }

    impl Child {
        // Pretend that this takes a lot of resources to evaluate.
        fn an_expensive_computation(&self) -> Option<&str> {
            Some(&self.name)
        }
    }

    impl std::fmt::Display for Child {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "{} failed: \n  {}", self.name, self.parent)
        }
    }

    impl std::error::Error for Child {
        fn provide<'a>(&'a self, request: &mut Request<'a>) {
            // In general, we don't know if this call will provide
            // a `str` reference or not...
            self.parent.provide(request);

            // ...so we check to see if the `&str` is needed before
            // we run our expensive computation.
            if request.would_be_satisfied_by_ref_of::<str>() {
                if let Some(v) = self.an_expensive_computation() {
                    request.provide_ref::<str>(v);
                }
            }

            // The request will be satisfied now, regardless of if
            // the parent provided the reference or we did.
            assert!(!request.would_be_satisfied_by_ref_of::<str>());
        }
    }

    let parent = Parent(Some("parent".into()));
    let child = Child { parent, name: "child".into() };
    assert_eq!(Some("parent"), request_ref::<str>(&child));

    let parent = Parent(None);
    let child = Child { parent, name: "child".into() };
    assert_eq!(Some("child"), request_ref::<str>(&child));
}
