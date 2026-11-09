use core::any::TypeId;

type Static = fn(&'static u8) -> &'static u8;
type Borrowed = for<'a> fn(&'a u8) -> &'a u8;
type Renamed = for<'b> fn(&'b u8) -> &'b u8;

fn identity(value: &u8) -> &u8 {
    value
}

macro_rules! type_text {
    ($type:ty) => {
        stringify!($type)
    };
}

fn main() {
    assert_ne!(TypeId::of::<Static>(), TypeId::of::<Borrowed>());
    assert_eq!(TypeId::of::<Borrowed>(), TypeId::of::<Renamed>());

    let borrowed: Borrowed = identity;
    let fixed: Static = identity;
    let local = 43;
    static GLOBAL: u8 = 47;
    assert_eq!(*borrowed(&local), 43);
    assert_eq!(*fixed(&GLOBAL), 47);

    let elided = type_text!(&u8);
    let explicit = type_text!(&'static u8);
    let bound = type_text!(for<'a> fn(&'a u8) -> &'a u8);
    assert!(!elided.contains("'static"));
    assert!(explicit.contains("'static"));
    assert!(bound.contains("'a"));
}
