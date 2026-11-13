//@ proc-macro-aux-build: closed_enum_macro_kinds.rs

#[derive(closed_enum_macro_kinds::Total)]
struct Derived(u16);

#[closed_enum_macro_kinds::keep]
fn doubled(value: u16) -> u16 {
    value * 2
}

trait Read {
    fn read(&self) -> u16;
}

impl Read for Derived {
    fn read(&self) -> u16 {
        self.0
    }
}

fn read<T: Read>(value: &T) -> u16 {
    value.read()
}

fn main() {
    let value = Derived(closed_enum_macro_kinds::echo!(439));
    assert_eq!(value.total(), 872);
    assert_eq!(doubled(read(&value)), 878);
}
