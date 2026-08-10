// Extracted from library/core/src/marker.rs:775
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    trait ResType { }
    struct ParamType;
    mod foreign_lib {
        pub fn new(_: usize) -> *mut () { 42 as *mut () }
        pub fn do_stuff(_: *mut (), _: usize) {}
    }
    fn convert_params(_: ParamType) -> usize { 42 }
    use std::marker::PhantomData;

    struct ExternalResource<R> {
       resource_handle: *mut (),
       resource_type: PhantomData<R>,
    }

    impl<R: ResType> ExternalResource<R> {
        fn new() -> Self {
            let size_of_res = size_of::<R>();
            Self {
                resource_handle: foreign_lib::new(size_of_res),
                resource_type: PhantomData,
            }
        }

        fn do_stuff(&self, param: ParamType) {
            let foreign_params = convert_params(param);
            foreign_lib::do_stuff(self.resource_handle, foreign_params);
        }
    }
}
