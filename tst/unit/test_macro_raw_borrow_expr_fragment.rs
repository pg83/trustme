macro_rules! take_expr {
    ($value:expr) => {
        let _: *const [u8] = $value;
    };
}

fn main() {
    let values = [1_u8, 2, 3];
    take_expr!(&raw const values[0..2]);
}
