trait Foo {
    fn foo(&self) {}
}

impl Foo for [i32; 4] {}
impl Foo for [i64; 8] {}

fn array<T, const N: usize>() -> [T; N]
where
    [T; N]: Default,
{
    Default::default()
}

fn main() {
    let value = array();
    Foo::foo(&value);
    let _: [_; 4] = value;
}
