macro_rules! declare_static {
    (static $name:ident: $ty:ty = $value:expr) => {
        let $name: $ty = $value;
    };
}

fn main() {
    declare_static!(static VALUE: Vec<Vec<u32>>= vec![]);
}
