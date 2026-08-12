macro_rules! declare {
    (static $name:ident: $ty:ty = $value:expr) => {
        let $name: $ty = $value;
    };
}

fn main() {
    declare!(static _values: Vec<Vec<u32>>= vec![]);
}
