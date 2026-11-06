trait Select {
    type View;
}

struct Source;

impl Select for Source {
    type View = &'static u16;
}

fn read<S: Select<View = &'static u16>>(value: S::View) -> u16 {
    *value
}

fn main() {
    static VALUE: u16 = 197;
    assert_eq!(read::<Source>(&VALUE), 197);
}
