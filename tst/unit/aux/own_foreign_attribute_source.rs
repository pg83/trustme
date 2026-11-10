#[macro_export]
macro_rules! emit {
    ($receiver:ident) => {
        $receiver! {
            #[$crate::kept]
            fn original() -> u16 { 59 }
        }
    };
}
