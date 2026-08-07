// Extracted from library/core/src/error.rs:643
#![allow(unused)]
#![feature(error_generic_member_access)]
fn main() {

    use core::error::Request;

    #[derive(Debug)]
    struct SomeConcreteType { business: String, party: String }
    fn today_is_a_weekday() -> bool { true }

    impl std::fmt::Display for SomeConcreteType {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "{} failed", self.business)
        }
    }

    impl std::error::Error for SomeConcreteType {
        fn provide<'a>(&'a self, request: &mut Request<'a>) {
            request.provide_ref_with::<str>(|| {
                if today_is_a_weekday() {
                    &self.business
                } else {
                    &self.party
                }
            });
        }
    }
}
