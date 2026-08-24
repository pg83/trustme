use local_inner_macros_producer::exported;

macro_rules! inner_helper {
    () => {
        0usize
    };
}

const VALUE: usize = exported!();
const _: [(); 42] = [(); VALUE];

pub fn value() -> usize {
    VALUE
}
