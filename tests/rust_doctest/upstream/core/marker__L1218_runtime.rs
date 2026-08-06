// Extracted from library/core/src/marker.rs:1218
#![allow(unused)]
#![feature(arbitrary_self_types, derive_coerce_pointee)]
fn main() {
    use std::marker::CoercePointee;
    use std::ops::Deref;
    
    #[derive(CoercePointee)]
    #[repr(transparent)]
    struct MySmartPointer<T: ?Sized>(Box<T>);
    
    impl<T: ?Sized> Deref for MySmartPointer<T> {
        type Target = T;
        fn deref(&self) -> &T {
            &self.0
        }
    }
    
    // You can always define this trait. (as long as you have #![feature(arbitrary_self_types)])
    trait MyTrait {
        fn func(self: MySmartPointer<Self>);
    }
    
    // But using `dyn MyTrait` requires #[derive(CoercePointee)].
    fn call_func(value: MySmartPointer<dyn MyTrait>) {
        value.func();
    }
}
