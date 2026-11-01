#![feature(const_type_id)]

use std::any::TypeId;
union Identity { id: TypeId, bytes: [u8; 16] }
const VALUE: Identity = Identity { id: TypeId::of::<(u8, u16)>() };

fn main() {
    let value = std::hint::black_box(VALUE);
    unsafe {
        assert_eq!(value.id, TypeId::of::<(u8, u16)>());
        assert_ne!(value.id, TypeId::of::<(u16, u8)>());
    }
}
