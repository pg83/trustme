use std::ops::Deref;

struct Fields {
    value: u32,
}

enum Access {
    Present(Fields),
}

impl Deref for Access {
    type Target = Fields;

    fn deref(&self) -> &Fields {
        match self {
            Access::Present(fields) => fields,
        }
    }
}

fn read(value: Access) -> u32 {
    value.value
}

fn main() {
    assert_eq!(read(Access::Present(Fields { value: 649 })), 649);
}
