trait Value: Send + Sync {}

impl<T: Send + Sync> Value for T {}

const fn keep_auto_traits(value: &dyn Value) -> &(dyn Send + Sync) {
    value
}

#[used]
static VALUE: &(dyn Send + Sync) = keep_auto_traits(&42u32);

fn main() {
    assert_eq!(core::mem::size_of_val(VALUE), core::mem::size_of::<u32>());
    assert_eq!(core::mem::align_of_val(VALUE), core::mem::align_of::<u32>());
}
