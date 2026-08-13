trait Value: Sync {}

impl Value for u32 {}

#[used]
static VALUE: &dyn Value = &42u32;

fn main() {
    assert_eq!(core::mem::size_of_val(VALUE), core::mem::size_of::<u32>());
    assert_eq!(core::mem::align_of_val(VALUE), core::mem::align_of::<u32>());
}
