//@ edition: 2021
//@ proc-macro-aux-build: test_cov99_w07_path_identity.rs

use test_cov99_w07_path_identity::identity;

trait Source {
    type Item;
}

impl Source for u8 {
    type Item = u16;
}

macro_rules! path_items {
    ($ty:ty, $path:path) => {
        #[identity]
        struct Captured<T: Source> {
            captured_type: ($ty,),
            captured_path: *const $path,
            projection: <T as Source>::Item,
        }
    };
}

path_items!(T, T);

#[identity]
fn unpack(Captured { captured_type, captured_path, projection }: Captured<u8>) -> (u8, u16, bool) {
    (captured_type.0, projection, captured_path.is_null())
}

fn main() {
    let value = Captured::<u8> {
        captured_type: (7,),
        captured_path: std::ptr::null(),
        projection: 19,
    };
    assert_eq!(unpack(value), (7, 19, true));
}
