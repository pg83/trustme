use std::any::type_name;

struct Holder<T>(T);

impl<T> Holder<T> {
    fn method(&self) -> &'static str {
        fn local() {}

        fn name_of<T>(_: T) -> &'static str {
            type_name::<T>()
        }

        name_of(local)
    }
}

fn local_trait_name() {
    trait Local {
        type Assoc;
    }

    assert_eq!(
        type_name::<dyn Local<Assoc = i32> + Send + Sync>(),
        "dyn test_type_name_local_items::local_trait_name::Local<Assoc = i32> \
         + core::marker::Send + core::marker::Sync",
    );
}

fn main() {
    assert_eq!(
        Holder(()).method(),
        "test_type_name_local_items::Holder<_>::method::local",
    );
    local_trait_name();
}
