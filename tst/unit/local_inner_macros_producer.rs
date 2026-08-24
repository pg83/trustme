#[doc(hidden)]
#[macro_export]
macro_rules! inner_helper {
    () => {
        41usize
    };
}

#[macro_export(local_inner_macros)]
macro_rules! exported {
    () => {
        inner_helper!() + 1
    };
}
