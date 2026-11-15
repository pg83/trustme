//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

mod u8 {
    pub struct Marker;
}

impl Alias {
    pub fn accepts(value: Self) -> bool {
        match value {
            Self => true,
        }
    }
}

type Alias = u8::Marker;
