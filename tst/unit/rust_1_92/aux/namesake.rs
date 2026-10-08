// A crate whose name the unit that depends on it takes for itself too.
#[macro_export]
macro_rules! seven {
    () => {
        7u8
    };
}

pub struct Marker(pub u8);
