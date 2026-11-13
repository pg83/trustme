//@ edition: 2021

trait HasItem {
    type Item;
}

impl HasItem for u8 {
    type Item = u16;
}

macro_rules! derived_paths {
    ($name:ident, $ty:ty, $path:path) => {
        #[derive(Clone)]
        struct $name<T: HasItem> {
            captured_type: ($ty,),
            captured_path: *const $path,
            projection: T::Item,
        }
    };
}

derived_paths!(Captured, T, T);

fn main() {
    let original = Captured::<u8> {
        captured_type: (7,),
        captured_path: std::ptr::null(),
        projection: 19,
    };
    let cloned = original.clone();
    assert_eq!(cloned.captured_type.0, 7);
    assert_eq!(cloned.captured_path, original.captured_path);
    assert_eq!(cloned.projection, 19);
}
