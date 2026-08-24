macro_rules! through_local_macro {
    ($value:expr) => {
        qualified_extern_macro_2015_producer::external_macro!($value)
    };
}

const VALUE: usize = through_local_macro!(41usize);
const _: [(); 42] = [(); VALUE];

pub fn value() -> usize {
    VALUE
}
