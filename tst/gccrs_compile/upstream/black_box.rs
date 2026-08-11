// { dg-options "-fdump-tree-gimple" }

#![feature(rustc_attrs, lang_items)]

pub fn black_box<T>(dummy: T) -> T {
    // { dg-final { scan-tree-dump-times {memory} 1 gimple } }
    std::hint::black_box(dummy)
}

fn my_function(a: i32) -> i32 {
    a
}

fn main() {
    let dummy: i32 = 42;
    let _ = black_box(my_function(dummy));
}
