// actix-web 4.15's `common_header!` arm `.. => ($item:ty)* $tm:ident{$($tf:item)*}`
// receives `crate::http::header::common_header_test!(..);` as items. A macro
// call whose path starts with `crate` is an item like one starting with a
// name, `self`, `super` or `::`.
macro_rules! items {
    ($($i:item)*) => {
        $($i)*
    };
}

macro_rules! make {
    ($name:ident) => {
        fn $name() -> u8 {
            7
        }
    };
}

pub(crate) use make;

items! {
    crate::make!(seven);
    self::make!(eight);
}

fn main() {
    assert_eq!(seven() + eight(), 14);
}
