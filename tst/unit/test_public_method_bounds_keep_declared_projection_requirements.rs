//@ crate-type: lib

pub trait Marker {
    type Tag;
}

impl Marker for u16 {
    type Tag = u8;
}

impl Marker for u32 {
    type Tag = u16;
}

pub struct Ready;
pub struct WrongTag;

pub trait Available {
    type Item;

    fn value() -> u16
    where
        Self::Item: Marker<Tag = u8>,
    {
        29
    }
}

impl Available for Ready {
    type Item = u16;
}

impl Available for WrongTag {
    type Item = u32;
}

pub trait Declared
where
    Self::Item: Marker<Tag = u8>,
{
    type Item;
}

impl Declared for Ready {
    type Item = u16;
}

fn use_declared<T: Declared>() {
    fn require_marker<U: Marker<Tag = u8>>() {}
    require_marker::<T::Item>();
}

#[no_mangle]
pub extern "C" fn public_value() -> u16 {
    use_declared::<Ready>();
    <Ready as Available>::value()
}
