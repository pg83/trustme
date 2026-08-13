use core::any::Any;

#[used]
static VALUE: &(dyn Any + Send + Sync) = &false;

fn main() {
    assert_eq!(core::mem::size_of_val(VALUE), core::mem::size_of::<bool>());
    assert_eq!(core::mem::align_of_val(VALUE), core::mem::align_of::<bool>());
}
