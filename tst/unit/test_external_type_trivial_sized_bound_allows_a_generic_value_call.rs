#![feature(extern_types, trivial_bounds)]

extern "C" {
    type Foreign;
}

fn forward(value: Foreign)
where
    Foreign: Sized,
{
    let _forwarded = std::convert::identity::<Foreign>(value);
}

fn main() {
    assert_eq!(
        std::mem::align_of::<*const Foreign>(),
        std::mem::align_of::<usize>(),
    );
}
