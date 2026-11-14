//@ proc-macro-aux-build: cov99_w264_keep.rs

macro_rules! shared_block {
    ($block:block) => {{
        let value = {
            #[cov99_w264_keep::keep]
            const VALUE: usize = $block;
            VALUE
        };
        const _: usize = $block;
        value
    }};
}

fn main() {
    let value = shared_block!({
        #[cfg(any())]
        const FILTERED_OUT: usize = 99;
        7
    });
    assert_eq!(value, 7);
}
