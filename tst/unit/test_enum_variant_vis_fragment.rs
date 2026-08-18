// A `$vis` fragment stands before an enum variant and expands to nothing, and a
// variant that a `cfg` removes is never checked for the visibility it wrote.
macro_rules! mac_variant {
    ($vis:vis $name:ident) => {
        enum $name {
            $vis Unit,
            $vis Tuple(u8, u16),
            $vis Struct { f: u8 },
        }
    };
}

mac_variant! { E }

#[cfg(false)]
enum Removed {
    pub U,
    pub(crate) T(u8),
    pub(super) S { f: String },
}

fn main() {
    let _ = E::Unit;
    let _ = E::Tuple(1, 2);
    let _ = E::Struct { f: 3 };
}
