//@ test-harness

const STRUCT_ID: i128 = -1;

trait HasField<const ID: i128> {}

struct Wrapper<T>(T);

impl<T> HasField<{ STRUCT_ID }> for Wrapper<T> {}

fn require_struct<T: HasField<-1>>() {}

fn generic<T>() {
    require_struct::<Wrapper<T>>();
}

enum Field {}

struct Pointer<T>(T);

impl<T> Pointer<T> {
    fn project<F, const ID: i128>(&self)
    where
        T: HasField<ID>,
    {
    }
}

fn generic_project<T>(pointer: Pointer<Wrapper<T>>) {
    pointer.project::<Field, { STRUCT_ID }>();
}

struct FieldWrapper<T>(T);

impl<T> HasField<1> for FieldWrapper<T> {}

fn generic_literal_project<T>(pointer: Pointer<FieldWrapper<T>>) {
    pointer.project::<Field, { 1 }>();
}

#[test]
fn const_path_matches_literal_in_generic_impl() {
    generic::<u8>();
    generic_project(Pointer(Wrapper(7u8)));
    generic_literal_project(Pointer(FieldWrapper(9u8)));
}
