#![feature(macro_metavar_expr_concat)]

macro_rules! build_items {
    (($($prefix:tt)*) $pattern:pat_param => $result:expr;
        $visibility:vis struct $name:ident { $field:ident: $ty:ty }
        $item:item
    ) => {
        $($prefix)* {
            () => {
                fn extract(value: Option<u16>) -> u16 {
                    match value {
                        $pattern => $result,
                        _ => 0,
                    }
                }
                $visibility struct $name { $field: $ty }
                $item
                fn ${concat(read_, $field)}(value: $name) -> u16 {
                    value.$field
                }
            };
        }
    };
}

build_items!(
    (macro_rules! materialize)
    Some(payload) => payload + seed();
    pub struct Record { value: u16 }
    fn seed() -> u16 { 823 }
);
materialize!();

fn main() {
    assert_eq!(extract(Some(7)), 830);
    assert_eq!(extract(None), 0);
    assert_eq!(read_value(Record { value: seed() }), 823);
}
