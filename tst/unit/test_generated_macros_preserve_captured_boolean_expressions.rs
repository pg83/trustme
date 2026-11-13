macro_rules! define {
    ($name:ident, $value:expr) => {
        macro_rules! $name {
            () => { $value };
        }
    };
}

define!(enabled, true);
define!(disabled, false);

fn main() {
    assert!(enabled!());
    assert!(!disabled!());
    assert_eq!([enabled!(), disabled!()], [true, false]);
}
